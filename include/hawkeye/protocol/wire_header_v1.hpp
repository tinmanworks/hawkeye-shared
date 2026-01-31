#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        WireHeaderV1

        This header prefixes EVERY datagram payload.
        It is encoded/decoded by the transport layer.

        DO NOT reorder fields.
        DO NOT change sizes.
        Append-only changes require a V2 header.
    */
#pragma pack(push, 1)
    struct WireHeaderV1 {
        uint16_t msg_type;   // schema ID within topic
        uint16_t flags;      // reserved (ack, reliable, etc.)
        uint32_t seq;        // sender sequence number
        uint64_t ts_us;      // optional timestamp (0 if unused)
        uint64_t topic;      // hawkeye::protocol::TOPIC_*
        uint64_t src;        // logical sender node ID
        uint64_t dst;        // logical destination node ID (0 = broadcast)
    };
#pragma pack(pop)

    static_assert(sizeof(WireHeaderV1) == 40, "WireHeaderV1 size mismatch");

} // namespace hawkeye::protocol
