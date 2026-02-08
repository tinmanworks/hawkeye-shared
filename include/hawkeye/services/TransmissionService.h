#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <memory>

#include <FrameKit/Networking/UdpDatagramTransport.hpp>
#include <FrameKit/Networking/DatagramChannel.hpp>
#include <FrameKit/Networking/EndpointText.hpp>

#include <hawkeye/protocol/wire_header_v1.hpp>
#include <hawkeye/protocol/heartbeat_v1.hpp>
#include <hawkeye/protocol/topics.hpp>

/*
    TransmissionService

    Purpose:
      - UDP datagram transport with versioned on-wire header codec (WireHeaderV1).
      - Multiple RX bindings (bind+poll) + one TX socket/channel.
      - Subscribe(topic, msg_type) dispatch based on decoded header fields.
      - Link health tracking:
          * last_rx updated on ANY delivered packet (via trampoline wrapper).
          * heartbeat RX handled internally.
      - Heartbeat TX:
          * enabled once a peer endpoint is known (discovery can set it).
          * targets are derived from peer address + peer_ports.

    Demo discovery use-case:
      - Init() can be called with peer_ip empty.
      - LinkLayer learns the peer endpoint from incoming ANNOUNCE/CLAIM and calls SetPeerFromEndpoint(from).
*/

class TransmissionService {
public:
    struct RxPort { uint16_t port = 0; };

    struct Params {
        std::vector<RxPort> rx_ports;

        bool     bind_tx_ephemeral = true;
        uint16_t tx_bind_port = 0;

        bool dual_stack = true;

        uint64_t node_id = 0;
        uint64_t peer_id = 0;

        // Optional at init. May be learned later.
        std::string peer_ip;

        // Ports to send heartbeat to once peer is known.
        std::vector<uint16_t> peer_ports;

        float  heartbeat_hz = 2.0f;
        double disconnect_sec = 2.5;
    };

    using HandlerFn = void (*)(void* user,
        const FrameKit::Net::HeaderView& hdr,
        FrameKit::Net::ConstByteSpan payload,
        const FrameKit::Net::Endpoint& from);

public:
    bool Init(const Params& p);
    void Shutdown();

    void Tick(double dt_seconds);

    /*
        Subscribe to (topic, msg_type) on all RX channels.

        Internally we wrap the handler with a trampoline that updates:
          - Health.rx_packets
          - Health.last_rx

        Note:
          - Heartbeat is already subscribed internally by the service.
          - Layers should generally NOT subscribe to MSG_HEARTBEAT_V1 directly
            to avoid confusing double-accounting; use GetHealth().last_rx for liveness.
    */
    bool Subscribe(uint64_t topic, uint16_t msg_type, HandlerFn fn, void* user);

    FrameKit::Net::NetErr SendTo(const FrameKit::Net::Endpoint& to,
        const FrameKit::Net::HeaderView& hdr,
        FrameKit::Net::ConstByteSpan payload);

    FrameKit::Net::NetErr SendToPeerPort(uint16_t port,
        const FrameKit::Net::HeaderView& hdr,
        FrameKit::Net::ConstByteSpan payload);

    // Dynamic peer control
    bool HasPeer() const { return m_HasPeer; }
    void ClearPeer();
    bool SetPeerFromEndpoint(const FrameKit::Net::Endpoint& from);

    struct Health {
        uint64_t rx_packets = 0;
        uint64_t tx_packets = 0;
        uint64_t rx_heartbeat = 0;
        uint64_t tx_heartbeat = 0;

        bool connected = false;

        std::chrono::steady_clock::time_point last_rx{};
        std::chrono::steady_clock::time_point last_hb_rx{};
        std::chrono::steady_clock::time_point last_hb_tx{};
    };

    const Health& GetHealth() const { return m_Health; }

    uint64_t GetNodeId() const { return m_Params.node_id; }
    uint64_t GetPeerId() const { return m_Params.peer_id; }

private:
    struct RxBinding {
        uint16_t port = 0;
        FrameKit::Net::UdpDatagramTransport udp{};
        FrameKit::Net::DatagramChannel      ch{};
        bool open = false;
    };

    struct SubCtx {
        TransmissionService* self = nullptr;
        HandlerFn fn = nullptr;
        void* user = nullptr;
    };

private:
    static FrameKit::Net::HeaderCodecOps MakeCodec();
    static bool MakeEndpointFromIpPort(const std::string& ip, uint16_t port, FrameKit::Net::Endpoint& out);
    static bool CopyAddressSetPort(const FrameKit::Net::Endpoint& from, uint16_t port, FrameKit::Net::Endpoint& out);

    void RegisterInternalHeartbeatHandlers();
    void TickHeartbeat(double dt_seconds);
    void UpdateConnectionState();

    static void Trampoline(void* user,
        const FrameKit::Net::HeaderView& hdr,
        FrameKit::Net::ConstByteSpan payload,
        const FrameKit::Net::Endpoint& from);

private:
    Params m_Params{};
    Health m_Health{};

    std::vector<RxBinding> m_Rx{};

    FrameKit::Net::UdpDatagramTransport m_TxUdp{};
    FrameKit::Net::DatagramChannel      m_TxCh{};
    bool m_TxOpen = false;

    bool m_HasPeer = false;
    FrameKit::Net::Endpoint m_PeerFrom{};

    std::vector<FrameKit::Net::Endpoint> m_HbTargets{};
    std::vector<std::unique_ptr<SubCtx>> m_SubCtx{};

    double   m_HbAccumSec = 0.0;
    uint32_t m_HbSeq = 0;
    uint32_t m_HbCounter = 0;
};
