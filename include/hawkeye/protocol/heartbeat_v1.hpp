#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        HeartbeatV1

        Used for:
        - link health
        - connection state
        - latency estimation (future)

        Sent periodically both directions.
    */
    static constexpr uint16_t MSG_HEARTBEAT_V1 = 1;

#pragma pack(push, 1)
    struct HeartbeatV1 {
        uint32_t counter;    // increments per sender
        uint32_t reserved;   // reserved for future fields
    };
#pragma pack(pop)

    static_assert(sizeof(HeartbeatV1) == 8, "HeartbeatV1 size mismatch");

} // namespace hawkeye::protocol
