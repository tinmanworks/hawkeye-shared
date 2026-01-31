#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        TelemetryPayloadV1

        SAHMS -> FlightDeck

        Keep this compact; telemetry is high-rate.
    */
    static constexpr uint16_t MSG_TELEMETRY_V1 = 1;

#pragma pack(push, 1)
    struct TelemetryV1 {
        uint32_t counter;
        float position_x;
        float position_y;
        float battery;   // 0.0 – 1.0
    };
#pragma pack(pop)

    static_assert(sizeof(TelemetryV1) == 16, "TelemetryV1 size mismatch");

} // namespace hawkeye::protocol
