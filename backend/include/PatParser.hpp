#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct PatProgram
{
    std::uint16_t programNumber = 0;

    std::uint16_t pmtPid = 0;
};

struct PatTable
{
    std::uint16_t transportStreamId = 0;

    std::uint8_t versionNumber = 0;

    bool currentNextIndicator = false;

    std::uint8_t sectionNumber = 0;

    std::uint8_t lastSectionNumber = 0;

    bool hasNetworkPid = false;

    std::uint16_t networkPid = 0;

    std::vector<PatProgram> programs;
};

class PatParser
{
public:
    static constexpr std::uint8_t TABLE_ID = 0x00;

    static constexpr std::size_t MIN_SECTION_SIZE = 12;

    static constexpr std::size_t MAX_SECTION_LENGTH = 1021;

    static bool parse(
        const std::uint8_t* data,
        std::size_t size,
        PatTable& table
    );

    static std::uint32_t calculateCrc32(
        const std::uint8_t* data,
        std::size_t size
    );
};
