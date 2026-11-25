#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class StreamCategory
{
    Video,
    Audio,
    Other
};

struct PmtElementaryStream
{
    std::uint8_t streamType = 0;

    std::uint16_t elementaryPid = 0;

    std::uint16_t esInfoLength = 0;

    StreamCategory category =
        StreamCategory::Other;

    std::vector<std::uint8_t> descriptors;
};

struct PmtTable
{
    std::uint16_t programNumber = 0;

    std::uint8_t versionNumber = 0;

    bool currentNextIndicator = false;

    std::uint8_t sectionNumber = 0;

    std::uint8_t lastSectionNumber = 0;

    std::uint16_t pcrPid = 0;

    std::uint16_t programInfoLength = 0;

    std::vector<std::uint8_t> programDescriptors;

    std::vector<PmtElementaryStream> streams;
};

class PmtParser
{
public:
    static constexpr std::uint8_t TABLE_ID = 0x02;

    static constexpr std::size_t MIN_SECTION_SIZE = 16;

    static constexpr std::size_t MAX_SECTION_LENGTH = 1021;

    static bool parse(
        const std::uint8_t* data,
        std::size_t size,
        PmtTable& table
    );

    static StreamCategory classifyStream(
        std::uint8_t streamType
    );

    static const char* streamTypeName(
        std::uint8_t streamType
    );

    static const char* categoryName(
        StreamCategory category
    );
};
