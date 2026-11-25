#include "PmtParser.hpp"
#include "PatParser.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    int passedTests = 0;
    int failedTests = 0;

    void check(bool condition, const char* name)
    {
        if (condition)
        {
            ++passedTests;

            std::cout << "[PASS] " << name << '\n';
        }
        else
        {
            ++failedTests;

            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    void appendCrc(
        std::vector<std::uint8_t>& section
    )
    {
        const std::uint32_t crc =
            PatParser::calculateCrc32(
                section.data(),
                section.size()
            );

        section.push_back(
            static_cast<std::uint8_t>(crc >> 24)
        );

        section.push_back(
            static_cast<std::uint8_t>(crc >> 16)
        );

        section.push_back(
            static_cast<std::uint8_t>(crc >> 8)
        );

        section.push_back(
            static_cast<std::uint8_t>(crc)
        );
    }

    void updateCrc(
        std::vector<std::uint8_t>& section
    )
    {
        section.resize(section.size() - 4);

        appendCrc(section);
    }

    std::vector<std::uint8_t> createPmt()
    {
        std::vector<std::uint8_t> section = {
            0x02,       // Table ID
            0xB0, 0x17, // Section length = 23
            0x00, 0x01, // Program number
            0xC1,       // Version 0, current
            0x00,       // Section number
            0x00,       // Last section number

            0xE1, 0x00, // PCR PID = 256

            0xF0, 0x00, // Program info length

            // H.264 video stream

            0x1B,
            0xE1, 0x00, // Video PID = 256
            0xF0, 0x00,

            // AAC audio stream

            0x0F,
            0xE1, 0x01, // Audio PID = 257
            0xF0, 0x00
        };

        appendCrc(section);

        return section;
    }

    bool parse(
        const std::vector<std::uint8_t>& section,
        PmtTable& table
    )
    {
        return PmtParser::parse(
            section.data(),
            section.size(),
            table
        );
    }

    void testValidPmt()
    {
        const auto section = createPmt();

        PmtTable table{};

        check(
            parse(section, table) &&
            table.programNumber == 1 &&
            table.pcrPid == 256 &&
            table.streams.size() == 2,
            "Valid PMT section"
        );
    }

    void testVideoStream()
    {
        const auto section = createPmt();

        PmtTable table{};

        check(
            parse(section, table) &&
            table.streams.size() == 2 &&
            table.streams[0].streamType == 0x1B &&
            table.streams[0].elementaryPid == 256 &&
            table.streams[0].category ==
                StreamCategory::Video,
            "H.264 video stream extraction"
        );
    }

    void testAudioStream()
    {
        const auto section = createPmt();

        PmtTable table{};

        check(
            parse(section, table) &&
            table.streams.size() == 2 &&
            table.streams[1].streamType == 0x0F &&
            table.streams[1].elementaryPid == 257 &&
            table.streams[1].category ==
                StreamCategory::Audio,
            "AAC audio stream extraction"
        );
    }

    void testInvalidCrc()
    {
        auto section = createPmt();

        section.back() ^= 0x01;

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid PMT CRC"
        );
    }

    void testWrongTableId()
    {
        auto section = createPmt();

        section[0] = 0x00;

        updateCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid PMT table ID"
        );
    }

    void testTruncatedSection()
    {
        auto section = createPmt();

        section.pop_back();

        PmtTable table{};

        check(
            !parse(section, table),
            "Truncated PMT section"
        );
    }

    void testNullInput()
    {
        PmtTable table{};

        check(
            !PmtParser::parse(
                nullptr, 26, table
            ),
            "Null PMT input"
        );
    }

    void testVersion()
    {
        auto section = createPmt();

        section[5] =
            static_cast<std::uint8_t>(
                0xC0 | (12 << 1) | 1
            );

        updateCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.versionNumber == 12,
            "PMT version extraction"
        );
    }

    void testNextTable()
    {
        auto section = createPmt();

        section[5] &= 0xFE;

        updateCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            !table.currentNextIndicator,
            "Next PMT table indicator"
        );
    }

    void testProgramNumber()
    {
        auto section = createPmt();

        section[4] = 3;

        updateCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.programNumber == 3,
            "Dynamic program number"
        );
    }

    void testPcrPid()
    {
        auto section = createPmt();

        section[8] = 0xE1;
        section[9] = 0x20;

        updateCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.pcrPid == 288,
            "Dynamic PCR PID"
        );
    }

    void testInvalidProgramInfoLength()
    {
        auto section = createPmt();

        section[10] = 0xFF;
        section[11] = 0xFF;

        updateCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid program info length"
        );
    }

    void testInvalidEsInfoLength()
    {
        auto section = createPmt();

        section[15] = 0xFF;
        section[16] = 0xFF;

        updateCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid ES info length"
        );
    }

    void testProgramDescriptors()
    {
        auto section = createPmt();

        section.resize(section.size() - 4);

        section[2] = 0x1A;
        section[11] = 3;

        section.insert(
            section.begin() + 12,
            {0x80, 0x01, 0xAA}
        );

        appendCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.programInfoLength == 3 &&
            table.programDescriptors.size() == 3 &&
            table.streams.size() == 2,
            "Program descriptor extraction"
        );
    }

    void testEsDescriptors()
    {
        auto section = createPmt();

        section.resize(section.size() - 4);

        section[2] = 0x1D;
        section[21] = 6;

        section.insert(
            section.begin() + 22,
            {
                0x0A, 0x04,
                'e', 'n', 'g', 0x00
            }
        );

        appendCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.streams.size() == 2 &&
            table.streams[1].esInfoLength == 6 &&
            table.streams[1].descriptors.size() == 6,
            "Elementary stream descriptor extraction"
        );
    }

    void testInvalidDescriptor()
    {
        auto section = createPmt();

        section.resize(section.size() - 4);

        section[2] = 0x1A;
        section[11] = 3;

        section.insert(
            section.begin() + 12,
            {0x80, 0x05, 0xAA}
        );

        appendCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Malformed descriptor rejected"
        );
    }

    void testUnknownStream()
    {
        auto section = createPmt();

        section[12] = 0x99;

        updateCrc(section);

        PmtTable table{};

        check(
            parse(section, table) &&
            table.streams.size() == 2 &&
            table.streams[0].category ==
                StreamCategory::Other,
            "Unknown stream type preserved"
        );
    }

    void testInvalidReservedBits()
    {
        auto section = createPmt();

        section[8] &= 0x1F;

        updateCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid PCR PID reserved bits"
        );
    }

    void testInvalidSectionNumber()
    {
        auto section = createPmt();

        section[6] = 1;
        section[7] = 1;

        updateCrc(section);

        PmtTable table{};

        check(
            !parse(section, table),
            "Invalid PMT section number"
        );
    }

    void testStreamClassification()
    {
        check(
            PmtParser::classifyStream(0x24) ==
                StreamCategory::Video &&
            PmtParser::classifyStream(0x11) ==
                StreamCategory::Audio &&
            PmtParser::classifyStream(0x06) ==
                StreamCategory::Other,
            "Stream type classification"
        );
    }
}

int main()
{
    std::cout << "IPTV M10 PMT Parser Tests\n\n";

    testValidPmt();
    testVideoStream();
    testAudioStream();
    testInvalidCrc();
    testWrongTableId();
    testTruncatedSection();
    testNullInput();
    testVersion();
    testNextTable();
    testProgramNumber();
    testPcrPid();
    testInvalidProgramInfoLength();
    testInvalidEsInfoLength();
    testProgramDescriptors();
    testEsDescriptors();
    testInvalidDescriptor();
    testUnknownStream();
    testInvalidReservedBits();
    testInvalidSectionNumber();
    testStreamClassification();

    std::cout << "\nPassed: "
              << passedTests << '\n';

    std::cout << "Failed: "
              << failedTests << '\n';

    return failedTests == 0 ? 0 : 1;
}
