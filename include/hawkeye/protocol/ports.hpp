#pragma once
#include <cstdint>

namespace hawkeye::protocol::ports {

    /*
        Hawkeye UDP Port Contract (Defaults)

        These ports define "where" traffic classes arrive.
        Topics/msg_types define "what" the traffic means.

        Applications MAY override these via CLI/config for deployments,
        but these defaults must remain stable for development and testing.
    */

    // SAHMS -> FlightDeck telemetry arrives here (FlightDeck binds RX here)
    static constexpr uint16_t TELEMETRY_RX = 5000;

    // FlightDeck -> SAHMS control arrives here (SAHMS binds RX here)
    static constexpr uint16_t CONTROL_RX = 5001;

    // Future (if you decide to separate video onto its own port)
    static constexpr uint16_t VIDEO_RX = 5002;

    /*
        Heartbeat

        Heartbeat is mandatory, but it does not require a dedicated port.
        You can send it to any port(s) that the peer is already listening on.
        Default strategy: send heartbeat to both TELEMETRY_RX and CONTROL_RX.

        If you ever want a dedicated link-health port, define LINK_RX and bind it.
        For now, keep it simple and reuse existing RX ports.
    */
    // static constexpr uint16_t LINK_RX = 5003; // optional future
}
