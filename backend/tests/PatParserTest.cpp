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

    std::vector<std::uint8_t> createPat()
    {
        std::vector<std::uint8_t> section = {
            0x00,       // Table ID
            0xB0, 0x0D, // Section length = 13
            0x00, 0x01, // Transport Stream ID
            0xC1,       // Version 0, current
            0x00,       // Section number
            0x00,       // Last section number
            0x00, 0x01, // Program number
            0xF0, 0x00  // PMT PID = 4096
        };

        appendCrc(section);

        return section;
    }

    void testCrcReference()
    {
        const std::uint8_t data[] = {
            '1', '2', '3', '4', '5',
            '6', '7', '8', '9'
        };

        check(
            PatParser::calculateCrc32(
                data, sizeof(data)
            ) == 0x0376E6E7U,
            "MPEG-2 CRC32 reference value"
        );
    }

    void testValidPat()
    {
        const auto section = createPat();

        PatTable table{};

        check(
            PatParser::parse(
                section.data(),
                section.size(),
                table
            ) &&
            table.transportStreamId == 1 &&
            table.versionNumber == 0 &&
            table.currentNextIndicator &&
            table.programs.size() == 1 &&
            table.programs[0].programNumber == 1 &&
            table.programs[0].pmtPid == 4096,
            "Valid PAT section"
        );
    }

    void testInvalidCrc()
    {
        auto section = createPat();

        section.back() ^= 0x01;

        PatTable table{};

        check(
            !PatParser::parse(
                section.data(),
                section.size(),
                table
            ),
            "Invalid PAT CRC"
        );
    }

    void testWrongTableId()
    {
        auto section = createPat();

        section[0] = 0x02;

        updateCrc(section);

        PatTable table{};

        check(
            !PatParser::parse(
                section.data(),
                section.size(),
                table
            ),
            "Invalid PAT table ID"
        );
    }

    void testTruncatedSection()
    {
        const auto section = createPat();

        PatTable table{};

        check(
            !PatParser::parse(
                section.data(),
                section.size() - 1,
                table
            ),
            "Truncated PAT section"
        );
    }

    void testNullPointer()
    {
        PatTable table{};

        check(
            !PatParser::parse(
                nullptr, 16, table
            ),
            "Null PAT input"
        );
    }

    void testMultiplePrograms()
    {
        std::vector<std::uint8_t> section = {
            0x00, 0xB0, 0x11,
            0x00, 0x01,
            0xC1, 0x00, 0x00,

            0x00, 0x01,
            0xF0, 0x00,

            0x00, 0x02,
            0xF0, 0x01
        };

        appendCrc(section);

        PatTable table{};

        check(
            PatParser::parse(
                section.data(),
                section.size(),
                table
            ) &&
            table.programs.size() == 2 &&
            table.programs[0].pmtPid == 4096 &&
            table.programs[1].programNumber == 2 &&
            table.programs[1].pmtPid == 4097,
            "Multiple PAT programs"
        );
    }

    void testNetworkPid()
    {
        auto section = createPat();

        section[8] = 0x00;

        section[9] = 0x00;

        section[10] = 0xE0;

        section[11] = 0x10;

        updateCrc(section);

        PatTable table{};

        check(
            PatParser::parse(
                section.data(),
                section.size(),
                table
            ) &&
            table.hasNetworkPid &&
            table.networkPid == 16 &&
            table.programs.empty(),
            "Program zero network PID"
        );
    }

    void testVersion()
    {
        auto section = createPat();

        section[5] =
            static_cast<std::uint8_t>(
                0xC0 | (17 << 1) | 1
            );

        updateCrc(section);

        PatTable table{};

        check(
            PatParser::parse(
                section.data(),
                section.size(),
                table
            ) &&
            table.versionNumber == 17,
            "PAT version extraction"
        );
    }

    void testNextTable()
    {
        auto section = createPat();

        section[5] &= 0xFE;

        updateCrc(section);

        PatTable table{};

        check(
            PatParser::parse(
                section.data(),
                section.size(),
                table
            ) &&
            !table.currentNextIndicator,
            "Next PAT table indicator"
        );
    }

    void testInvalidSectionNumber()
    {
        auto section = createPat();

        section[6] = 1;

        section[7] = 0;

        updateCrc(section);

        PatTable table{};

        check(
            !PatParser::parse(
                section.data(),
                section.size(),
                table
            ),
            "Invalid PAT section number"
        );
    }

    void testInvalidSyntax()
    {
        auto section = createPat();

        section[1] &= 0x7F;

        updateCrc(section);

        PatTable table{};

        check(
            !PatParser::parse(
                section.data(),
                section.size(),
                table
            ),
            "Invalid section syntax indicator"
        );
    }
}

int main()
{
    std::cout << "IPTV M10 PAT Parser Tests\n\n";

    testCrcReference();
    testValidPat();
    testInvalidCrc();
    testWrongTableId();
    testTruncatedSection();
    testNullPointer();
    testMultiplePrograms();
    testNetworkPid();
    testVersion();
    testNextTable();
    testInvalidSectionNumber();
    testInvalidSyntax();

    std::cout << "\nPassed: "
              << passedTests << '\n';

    std::cout << "Failed: "
              << failedTests << '\n';

    return failedTests == 0 ? 0 : 1;
}
