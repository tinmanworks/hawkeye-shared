#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        Topics are high-level routing channels.
        They MUST remain stable once released.
    */
    enum : uint64_t {
        TOPIC_LINK      = 1,    // link health, heartbeat
        TOPIC_TELEMETRY = 100,  // SAHMS -> FlightDeck
        TOPIC_CONTROL   = 101,  // FlightDeck -> SAHMS
        TOPIC_VIDEO     = 200   // future use
    };

} // namespace hawkeye::protocol
