#pragma once
#include <cstdint>

namespace hawkeye::protocol {

/*
    Link handshake messages (demo)

    These run on TOPIC_LINK alongside HeartbeatV1.

    Flow (single peer demo):
      SAHMS periodically broadcasts ANNOUNCE on TELEMETRY_RX port
      FlightDeck receives ANNOUNCE, learns endpoint, sends CLAIM unicast
      SAHMS receives CLAIM, stores controller endpoint, replies ACK unicast
      TransmissionService starts sending heartbeat once peer endpoint is known
*/

static constexpr uint16_t MSG_LINK_ANNOUNCE_V1 = 2;
static constexpr uint16_t MSG_LINK_CLAIM_V1    = 3;
static constexpr uint16_t MSG_LINK_ACK_V1      = 4;

#pragma pack(push, 1)

struct LinkAnnounceV1 {
    uint64_t node_id = 0;   // sender node id (SAHMS)
    uint32_t flags   = 0;   // reserved
    uint32_t reserved = 0;
};

struct LinkClaimV1 {
    uint64_t node_id = 0;   // sender node id (FlightDeck)
    uint32_t flags   = 0;
    uint32_t reserved = 0;
};

struct LinkAckV1 {
    uint64_t node_id = 0;   // sender node id (SAHMS)
    uint32_t flags   = 0;
    uint32_t reserved = 0;
};

#pragma pack(pop)

static_assert(sizeof(LinkAnnounceV1) == 16);
static_assert(sizeof(LinkClaimV1)    == 16);
static_assert(sizeof(LinkAckV1)      == 16);

} // namespace hawkeye::protocol
