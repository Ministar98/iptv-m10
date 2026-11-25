#pragma once

#include <cstddef>
#include <cstdint>

struct RtpPacket
{
    std::uint8_t version = 0;

    bool padding = false;
    bool extension = false;

    std::uint8_t csrcCount = 0;

    bool marker = false;

    std::uint8_t payloadType = 0;

    std::uint16_t sequenceNumber = 0;

    std::uint32_t timestamp = 0;
    std::uint32_t ssrc = 0;

    std::size_t headerSize = 0;

    const std::uint8_t* payload = nullptr;

    std::size_t payloadSize = 0;
};

class RtpParser
{
public:
    static constexpr std::size_t MIN_HEADER_SIZE = 12;

    static constexpr std::uint8_t RTP_VERSION = 2;

    static constexpr std::uint8_t MPEG_TS_PAYLOAD_TYPE = 33;

    static bool parse(
        const std::uint8_t* data,
        std::size_t size,
        RtpPacket& packet
    );
};
