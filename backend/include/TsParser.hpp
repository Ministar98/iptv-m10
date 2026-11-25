#pragma once

#include <cstddef>
#include <cstdint>

struct TsPacket
{
    std::uint8_t syncByte = 0;

    bool transportError = false;
    bool payloadUnitStart = false;
    bool transportPriority = false;

    std::uint16_t pid = 0;

    std::uint8_t scramblingControl = 0;
    std::uint8_t adaptationFieldControl = 0;
    std::uint8_t continuityCounter = 0;

    bool hasAdaptationField = false;
    bool hasPayload = false;

    std::uint8_t adaptationFieldLength = 0;

    bool discontinuityIndicator = false;
    bool randomAccessIndicator = false;

    bool hasPcr = false;

    std::uint64_t pcrBase = 0;
    std::uint16_t pcrExtension = 0;
    std::uint64_t pcr = 0;

    const std::uint8_t* payload = nullptr;

    std::size_t payloadSize = 0;
};

class TsParser
{
public:
    static constexpr std::size_t PACKET_SIZE = 188;

    static constexpr std::size_t HEADER_SIZE = 4;

    static constexpr std::uint8_t SYNC_BYTE = 0x47;

    static constexpr std::uint16_t PAT_PID = 0x0000;

    static constexpr std::uint16_t NULL_PID = 0x1FFF;

    static bool parse(
        const std::uint8_t* data,
        std::size_t size,
        TsPacket& packet
    );
};
