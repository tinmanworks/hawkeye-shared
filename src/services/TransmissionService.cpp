#include "hawkeye/services/TransmissionService.h"

#include <FrameKit/Debug/Log.h>

using namespace hawkeye::protocol;

namespace {

    static void EndpointToText(const FrameKit::Net::Endpoint& ep, char* out, size_t out_sz) {
        if (!out || out_sz == 0) return;
        out[0] = 0;
        (void)FrameKit::Net::FormatEndpoint(ep, out, out_sz);
    }

    struct RateLimiter {
        double acc = 0.0;
        double every = 1.0;
        bool Step(double dt) {
            if (dt < 0.0 || dt > 5.0) dt = 0.0;
            acc += dt;
            if (acc >= every) { acc = 0.0; return true; }
            return false;
        }
    };

    // -----------------------------------------
    // Header codec glue (WireHeaderV1)
    // -----------------------------------------
    static bool DecodeHdr(void*, FrameKit::Net::ConstByteSpan bytes,
        FrameKit::Net::HeaderView& out, size_t& out_header_size)
    {
        if (bytes.size < sizeof(WireHeaderV1)) return false;

        WireHeaderV1 wh{};
        std::memcpy(&wh, bytes.data, sizeof(WireHeaderV1));

        out.msg_type = wh.msg_type;
        out.flags = wh.flags;
        out.seq = wh.seq;
        out.ts_us = wh.ts_us;
        out.topic = wh.topic;
        out.src = wh.src;
        out.dst = wh.dst;

        out.extra_ptr = nullptr;
        out.extra_size = 0;

        out_header_size = sizeof(WireHeaderV1);
        return true;
    }

    static size_t EncodeHdr(void*, FrameKit::Net::ByteSpan out_bytes,
        const FrameKit::Net::HeaderView& in)
    {
        if (out_bytes.size < sizeof(WireHeaderV1)) return 0;

        WireHeaderV1 wh{};
        wh.msg_type = (uint16_t)in.msg_type;
        wh.flags = (uint16_t)in.flags;
        wh.seq = (uint32_t)in.seq;
        wh.ts_us = (uint64_t)in.ts_us;
        wh.topic = (uint64_t)in.topic;
        wh.src = (uint64_t)in.src;
        wh.dst = (uint64_t)in.dst;

        std::memcpy(out_bytes.data, &wh, sizeof(WireHeaderV1));
        return sizeof(WireHeaderV1);
    }

} // namespace

FrameKit::Net::HeaderCodecOps TransmissionService::MakeCodec()
{
    FrameKit::Net::HeaderCodecOps ops{};
    ops.user = nullptr;
    ops.decode = &DecodeHdr;
    ops.encode = &EncodeHdr;
    return ops;
}

bool TransmissionService::MakeEndpointFromIpPort(const std::string& ip, uint16_t port, FrameKit::Net::Endpoint& out)
{
    std::string s;

    const bool has_brackets = (!ip.empty() && ip.front() == '[');
    const bool looks_ipv6 = (ip.find(':') != std::string::npos);

    if (has_brackets)        s = ip + ":" + std::to_string((unsigned)port);
    else if (looks_ipv6)     s = "[" + ip + "]:" + std::to_string((unsigned)port);
    else                     s = ip + ":" + std::to_string((unsigned)port);

    return FrameKit::Net::ParseEndpoint(s.c_str(), out, 0) == FrameKit::Net::NetErr::Ok;
}

bool TransmissionService::CopyAddressSetPort(const FrameKit::Net::Endpoint& from, uint16_t port, FrameKit::Net::Endpoint& out)
{
    out = from;

    if (out.family == FrameKit::Net::AddressFamily::IPv6) {
        out.v6.port_host = port;
        return true;
    }
    if (out.family == FrameKit::Net::AddressFamily::IPv4) {
        out.v4.port_host = port;
        return true;
    }
    return false;
}

void TransmissionService::ClearPeer()
{
    m_HasPeer = false;
    m_HbTargets.clear();
    std::memset(&m_PeerFrom, 0, sizeof(m_PeerFrom));
}

bool TransmissionService::SetPeerFromEndpoint(const FrameKit::Net::Endpoint& from)
{
    m_PeerFrom = from;
    m_HbTargets.clear();

    if (m_Params.peer_ports.empty()) {
        m_HasPeer = true;
        return true;
    }

    for (uint16_t port : m_Params.peer_ports) {
        FrameKit::Net::Endpoint ep{};
        if (!CopyAddressSetPort(from, port, ep)) {
            FK_ERROR("[TxSvc] SetPeerFromEndpoint failed: unsupported address family");
            return false;
        }
        m_HbTargets.push_back(ep);
    }

    m_HasPeer = true;

    char from_txt[128]{};
    EndpointToText(from, from_txt, sizeof(from_txt));
    FK_INFO("[TxSvc] Peer learned from endpoint: {} (hb_targets={})", from_txt, (int)m_HbTargets.size());
    return true;
}

bool TransmissionService::Init(const Params& p)
{
    m_Params = p;

    FK_INFO("[TxSvc] Init begin: node_id={} peer_id={} peer_ip='{}' rx_ports={} peer_ports={} hb_hz={} disc_sec={} dual_stack={} tx_ephemeral={}",
        (unsigned long long)m_Params.node_id,
        (unsigned long long)m_Params.peer_id,
        m_Params.peer_ip,
        (int)m_Params.rx_ports.size(),
        (int)m_Params.peer_ports.size(),
        m_Params.heartbeat_hz,
        m_Params.disconnect_sec,
        (int)m_Params.dual_stack,
        (int)m_Params.bind_tx_ephemeral
    );

    if (m_Params.heartbeat_hz <= 0.0f) {
        FK_ERROR("[TxSvc] Init failed: heartbeat_hz must be > 0");
        return false;
    }

    ClearPeer();

    const auto codec = MakeCodec();

    FrameKit::Net::DatagramChannelConfig cfg{};
    cfg.mode = FrameKit::Net::WireMode::Framed;
    cfg.use_header_crc = true;
    cfg.use_payload_crc = false;
    cfg.max_datagram = 1400;

    // -----------------------
    // Open RX bindings
    // -----------------------
    m_Rx.clear();
    m_Rx.reserve(m_Params.rx_ports.size());

    for (const auto& rp : m_Params.rx_ports) {
        m_Rx.emplace_back();
        auto& b = m_Rx.back();

        b.port = rp.port;
        FK_INFO("[TxSvc] RX setup: port={}", b.port);

        FrameKit::Net::Endpoint bind{};
        if (FrameKit::Net::ParseEndpoint("[::]:0", bind, 0) != FrameKit::Net::NetErr::Ok) {
            bind = FrameKit::Net::Endpoint::FromV4(0, 0);
            FK_WARN("[TxSvc] RX ParseEndpoint([::]:0) failed; fallback to v4 any");
        }

        if (bind.family == FrameKit::Net::AddressFamily::IPv6) bind.v6.port_host = b.port;
        else                                                   bind.v4.port_host = b.port;

        auto e = b.udp.Open();
        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] RX Open failed (port {}) err={}", b.port, (int)e);
            return false;
        }

        (void)b.udp.SetReuseAddr(true);
        (void)b.udp.SetNonBlocking(true);
        if (m_Params.dual_stack) (void)b.udp.SetIPv6Only(false);

        e = b.udp.Bind(bind);
        if (e != FrameKit::Net::NetErr::Ok) {
            char bind_txt[128]{};
            EndpointToText(bind, bind_txt, sizeof(bind_txt));
            FK_ERROR("[TxSvc] RX Bind failed port={} bind={} err={}", b.port, bind_txt, (int)e);
            return false;
        }

        {
            char bind_txt[128]{};
            EndpointToText(bind, bind_txt, sizeof(bind_txt));
            FK_INFO("[TxSvc] RX bound: {}", bind_txt);
        }

        e = b.ch.Open(&b.udp, cfg, codec);
        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] RX Channel open failed on port {} err={}", b.port, (int)e);
            return false;
        }

        b.open = true;
        FK_INFO("[TxSvc] RX ready: port={}", b.port);
    }

    // -----------------------
    // Open TX socket/channel
    // -----------------------
    {
        FK_INFO("[TxSvc] TX setup: bind_ephemeral={} tx_bind_port={}",
            (int)m_Params.bind_tx_ephemeral, (unsigned)m_Params.tx_bind_port);

        FrameKit::Net::Endpoint bind{};
        if (FrameKit::Net::ParseEndpoint("[::]:0", bind, 0) != FrameKit::Net::NetErr::Ok) {
            bind = FrameKit::Net::Endpoint::FromV4(0, 0);
            FK_WARN("[TxSvc] TX ParseEndpoint([::]:0) failed; fallback to v4 any");
        }

        if (!m_Params.bind_tx_ephemeral) {
            if (bind.family == FrameKit::Net::AddressFamily::IPv6) bind.v6.port_host = m_Params.tx_bind_port;
            else                                                   bind.v4.port_host = m_Params.tx_bind_port;
        }

        auto e = m_TxUdp.Open();
        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] TX Open failed err={}", (int)e);
            return false;
        }

        (void)m_TxUdp.SetReuseAddr(true);
        (void)m_TxUdp.SetNonBlocking(true);
        if (m_Params.dual_stack) (void)m_TxUdp.SetIPv6Only(false);

        e = m_TxUdp.Bind(bind);
        if (e != FrameKit::Net::NetErr::Ok) {
            char bind_txt[128]{};
            EndpointToText(bind, bind_txt, sizeof(bind_txt));
            FK_ERROR("[TxSvc] TX Bind failed bind={} err={}", bind_txt, (int)e);
            return false;
        }

        {
            char bind_txt[128]{};
            EndpointToText(bind, bind_txt, sizeof(bind_txt));
            FK_INFO("[TxSvc] TX bound: {}", bind_txt);
        }

        e = m_TxCh.Open(&m_TxUdp, cfg, codec);
        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] TX Channel open failed err={}", (int)e);
            return false;
        }

        m_TxOpen = true;
        FK_INFO("[TxSvc] TX ready");
    }

    // Optional: allow static peer configuration (legacy/manual mode)
    if (!m_Params.peer_ip.empty() && !m_Params.peer_ports.empty()) {
        FrameKit::Net::Endpoint epFrom{};
        if (MakeEndpointFromIpPort(m_Params.peer_ip, m_Params.peer_ports[0], epFrom)) {
            (void)SetPeerFromEndpoint(epFrom);
        }
    }

    RegisterInternalHeartbeatHandlers();

    FK_INFO("[TxSvc] Init OK. rx_ports={} hb_targets={} has_peer={}",
        (int)m_Rx.size(), (int)m_HbTargets.size(), (int)m_HasPeer);

    return true;
}

void TransmissionService::Shutdown()
{
    FK_INFO("[TxSvc] Shutdown begin");

    m_SubCtx.clear();

    for (auto& r : m_Rx) {
        if (r.open) {
            FK_INFO("[TxSvc] RX close: port={}", r.port);
            r.ch.Close();
            r.udp.Close();
            r.open = false;
        }
    }
    m_Rx.clear();

    if (m_TxOpen) {
        FK_INFO("[TxSvc] TX close");
        m_TxCh.Close();
        m_TxUdp.Close();
        m_TxOpen = false;
    }

    ClearPeer();
    FK_INFO("[TxSvc] Shutdown end");
}

void TransmissionService::Trampoline(void* user,
    const FrameKit::Net::HeaderView& hdr,
    FrameKit::Net::ConstByteSpan payload,
    const FrameKit::Net::Endpoint& from)
{
    auto* ctx = static_cast<SubCtx*>(user);
    if (!ctx || !ctx->self) return;

    auto* self = ctx->self;

    // Count and timestamp ANY packet delivered to subscribers.
    self->m_Health.rx_packets++;
    self->m_Health.last_rx = std::chrono::steady_clock::now();

    if (ctx->fn) ctx->fn(ctx->user, hdr, payload, from);
}

bool TransmissionService::Subscribe(uint64_t topic, uint16_t msg_type, HandlerFn fn, void* user)
{
    FK_INFO("[TxSvc] Subscribe: topic={} msg_type={} on {} rx channels",
        (unsigned long long)topic, (unsigned)msg_type, (int)m_Rx.size());

    // Wrap user handler with a trampoline that updates Health.
    auto ctx = std::make_unique<SubCtx>();
    ctx->self = this;
    ctx->fn = fn;
    ctx->user = user;
    SubCtx* raw = ctx.get();
    m_SubCtx.push_back(std::move(ctx));

    bool ok = true;
    for (auto& r : m_Rx) {
        if (!r.open) continue;

        const auto e = r.ch.Subscribe(topic, msg_type, &TransmissionService::Trampoline, raw);
        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] Subscribe FAILED on port={} err={} topic={} msg={}",
                r.port, (int)e, (unsigned long long)topic, (unsigned)msg_type);
            ok = false;
        }
        else {
            FK_INFO("[TxSvc] Subscribe OK on port={} topic={} msg={}",
                r.port, (unsigned long long)topic, (unsigned)msg_type);
        }
    }
    return ok;
}

FrameKit::Net::NetErr TransmissionService::SendTo(const FrameKit::Net::Endpoint& to,
    const FrameKit::Net::HeaderView& hdr,
    FrameKit::Net::ConstByteSpan payload)
{
    if (!m_TxOpen) return FrameKit::Net::NetErr::Fail;

    const auto e = m_TxCh.SendTo(to, hdr, payload);
    m_Health.tx_packets++;

    if (e != FrameKit::Net::NetErr::Ok) {
        char to_txt[128]{};
        EndpointToText(to, to_txt, sizeof(to_txt));
        FK_WARN("[TxSvc] SendTo FAIL err={} to={} topic={} msg={} seq={} bytes={}",
            (int)e, to_txt,
            (unsigned long long)hdr.topic,
            (unsigned)hdr.msg_type,
            (unsigned)hdr.seq,
            (int)payload.size
        );
    }

    return e;
}

FrameKit::Net::NetErr TransmissionService::SendToPeerPort(uint16_t port,
    const FrameKit::Net::HeaderView& hdr,
    FrameKit::Net::ConstByteSpan payload)
{
    if (!m_HasPeer) {
        FK_WARN("[TxSvc] SendToPeerPort ignored: no peer set yet");
        return FrameKit::Net::NetErr::Fail;
    }

    FrameKit::Net::Endpoint ep{};
    if (!CopyAddressSetPort(m_PeerFrom, port, ep)) {
        FK_ERROR("[TxSvc] SendToPeerPort failed: bad endpoint family");
        return FrameKit::Net::NetErr::Fail;
    }

    return SendTo(ep, hdr, payload);
}

void TransmissionService::RegisterInternalHeartbeatHandlers()
{
    FK_INFO("[TxSvc] Register heartbeat RX handlers on {} channels", (int)m_Rx.size());

    for (auto& r : m_Rx) {
        if (!r.open) continue;

        const auto e = r.ch.Subscribe(
            TOPIC_LINK, MSG_HEARTBEAT_V1,
            [](void* u, const FrameKit::Net::HeaderView& hdr,
                FrameKit::Net::ConstByteSpan payload, const FrameKit::Net::Endpoint& from)
            {
                auto* self = static_cast<TransmissionService*>(u);

                // Heartbeat is tracked internally so "link alive" works even with no other traffic.
                // Layers should typically NOT subscribe to heartbeat directly; use GetHealth().last_rx.
                self->m_Health.rx_packets++;
                self->m_Health.rx_heartbeat++;

                const auto now = std::chrono::steady_clock::now();
                self->m_Health.last_rx = now;
                self->m_Health.last_hb_rx = now;

                static thread_local uint32_t s_Count = 0;
                s_Count++;
                const bool logNow = ((s_Count % 8) == 0);

                if (logNow) {
                    char from_txt[128]{};
                    EndpointToText(from, from_txt, sizeof(from_txt));

                    if (payload.size >= sizeof(HeartbeatV1)) {
                        HeartbeatV1 hb{};
                        std::memcpy(&hb, payload.data, sizeof(hb));
                        FK_INFO("[TxSvc] HB RX from={} seq={} hb_counter={} src={} dst={}",
                            from_txt,
                            (unsigned)hdr.seq,
                            (unsigned)hb.counter,
                            (unsigned long long)hdr.src,
                            (unsigned long long)hdr.dst
                        );
                    }
                    else {
                        FK_INFO("[TxSvc] HB RX from={} seq={} payload_bytes={}",
                            from_txt, (unsigned)hdr.seq, (int)payload.size);
                    }
                }
            },
            this
        );

        if (e != FrameKit::Net::NetErr::Ok) {
            FK_ERROR("[TxSvc] Heartbeat Subscribe FAILED on port={} err={}", r.port, (int)e);
        }
        else {
            FK_INFO("[TxSvc] Heartbeat Subscribe OK on port={}", r.port);
        }
    }
}

void TransmissionService::Tick(double dt_seconds)
{
    for (auto& r : m_Rx) {
        if (!r.open) continue;
        (void)r.ch.PollAll();
    }

    static RateLimiter summary{ 0.0, 1.0 };
    if (summary.Step(dt_seconds)) {
        FK_INFO("[TxSvc] Tick: rx_ports={} connected={} has_peer={} rx_pkts={} tx_pkts={} hb_rx={} hb_tx={}",
            (int)m_Rx.size(),
            (int)m_Health.connected,
            (int)m_HasPeer,
            (unsigned long long)m_Health.rx_packets,
            (unsigned long long)m_Health.tx_packets,
            (unsigned long long)m_Health.rx_heartbeat,
            (unsigned long long)m_Health.tx_heartbeat
        );
    }

    TickHeartbeat(dt_seconds);
    UpdateConnectionState();
}

void TransmissionService::TickHeartbeat(double dt_seconds)
{
    if (!m_TxOpen) return;
    if (!m_HasPeer) return;
    if (m_HbTargets.empty()) return;

    if (dt_seconds < 0.0 || dt_seconds > 1.0) dt_seconds = 0.0;
    m_HbAccumSec += dt_seconds;

    const double period = 1.0 / (double)m_Params.heartbeat_hz;

    static RateLimiter hbLog{ 0.0, 1.0 };
    const bool logNow = hbLog.Step(dt_seconds);

    int sent = 0;
    while (m_HbAccumSec >= period && sent < 4) {
        m_HbAccumSec -= period;
        ++sent;

        HeartbeatV1 hb{};
        hb.counter = ++m_HbCounter;
        hb.reserved = 0;

        FrameKit::Net::HeaderView hv{};
        hv.topic = TOPIC_LINK;
        hv.msg_type = MSG_HEARTBEAT_V1;
        hv.seq = ++m_HbSeq;
        hv.src = m_Params.node_id;
        hv.dst = m_Params.peer_id;

        for (const auto& ep : m_HbTargets) {
            (void)SendTo(ep,
                hv,
                FrameKit::Net::ConstByteSpan{
                    reinterpret_cast<const uint8_t*>(&hb),
                    sizeof(hb)
                });

            m_Health.tx_heartbeat++;
            m_Health.last_hb_tx = std::chrono::steady_clock::now();

            if (logNow) {
                char ep_txt[128]{};
                EndpointToText(ep, ep_txt, sizeof(ep_txt));
                FK_INFO("[TxSvc] HB TX -> {} seq={} hb_counter={}", ep_txt, (unsigned)hv.seq, (unsigned)hb.counter);
            }
        }
    }
}

void TransmissionService::UpdateConnectionState()
{
    using clock = std::chrono::steady_clock;

    if (m_Health.last_rx.time_since_epoch().count() == 0) {
        if (m_Health.connected) {
            m_Health.connected = false;
            FK_WARN("[TxSvc] DISCONNECTED (no rx yet)");
        }
        return;
    }

    const auto now = clock::now();
    const double age = std::chrono::duration<double>(now - m_Health.last_rx).count();
    const bool alive = (age <= m_Params.disconnect_sec);

    if (alive && !m_Health.connected) {
        m_Health.connected = true;
        FK_INFO("[TxSvc] CONNECTED (last_rx_age={}s)", age);
    }
    else if (!alive && m_Health.connected) {
        m_Health.connected = false;
        FK_WARN("[TxSvc] DISCONNECTED (last_rx_age={}s > {}s)", age, m_Params.disconnect_sec);
    }
}
