#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        ControlPayloadV1

        FlightDeck -> SAHMS

        "Latest wins" semantics.
    */
    static constexpr uint16_t MSG_CONTROL_V1 = 1;

#pragma pack(push, 1)
    struct ControlV1 {
        float vx;   // velocity x
        float vy;   // velocity y
        float vz;   // velocity z
    };
#pragma pack(pop)

    static_assert(sizeof(ControlV1) == 12, "ControlV1 size mismatch");

} // namespace hawkeye::protocol
