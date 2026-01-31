#pragma once
#include <cstdint>

namespace hawkeye::protocol {

    /*
        Protocol versioning

        Increment MAJOR if:
        - wire header layout changes
        - field meaning changes incompatibly

        Increment MINOR if:
        - new message types are added
        - new optional fields are appended

        PATCH is for documentation / comments only.
    */
    struct ProtocolVersion {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    };

    static constexpr ProtocolVersion CURRENT_VERSION {
        1,  // major
        0,  // minor
        0   // patch
    };

} // namespace hawkeye::protocol
