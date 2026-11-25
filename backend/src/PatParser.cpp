#include "PatParser.hpp"

#include <utility>

std::uint32_t PatParser::calculateCrc32(
    const std::uint8_t* data,
    std::size_t size
)
{
    constexpr std::uint32_t POLYNOMIAL =
        0x04C11DB7U;

    std::uint32_t crc = 0xFFFFFFFFU;

    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= static_cast<std::uint32_t>(
            data[i]
        ) << 24;

        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x80000000U) != 0)
            {
                crc = (crc << 1) ^ POLYNOMIAL;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool PatParser::parse(
    const std::uint8_t* data,
    std::size_t size,
    PatTable& table
)
{
    table = PatTable{};

    if (data == nullptr ||
        size < MIN_SECTION_SIZE)
    {
        return false;
    }

    if (data[0] != TABLE_ID)
    {
        return false;
    }

    // Section syntax indicator = 1.
    // Zero bit = 0.
    // Reserved bits = 11.

    if ((data[1] & 0xF0) != 0xB0)
    {
        return false;
    }

    const std::size_t sectionLength =
        (static_cast<std::size_t>(
            data[1] & 0x0F
        ) << 8) |
        data[2];

    if (sectionLength < 9 ||
        sectionLength > MAX_SECTION_LENGTH ||
        sectionLength + 3 != size)
    {
        return false;
    }

    // PAT entries have a fixed size of four bytes.

    if ((sectionLength - 9) % 4 != 0)
    {
        return false;
    }

    // CRC over a complete valid section,
    // including the transmitted CRC, equals zero.

    if (calculateCrc32(data, size) != 0)
    {
        return false;
    }

    PatTable result{};

    result.transportStreamId =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                data[3]
            ) << 8) |
            data[4]
        );

    if ((data[5] & 0xC0) != 0xC0)
    {
        return false;
    }

    result.versionNumber =
        (data[5] >> 1) & 0x1F;

    result.currentNextIndicator =
        (data[5] & 0x01) != 0;

    result.sectionNumber = data[6];

    result.lastSectionNumber = data[7];

    if (result.sectionNumber >
        result.lastSectionNumber)
    {
        return false;
    }

    // The final four bytes contain CRC32.

    const std::size_t entriesEnd =
        size - 4;

    for (std::size_t offset = 8;
         offset < entriesEnd;
         offset += 4)
    {
        const std::uint16_t programNumber =
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(
                    data[offset]
                ) << 8) |
                data[offset + 1]
            );

        if ((data[offset + 2] & 0xE0) != 0xE0)
        {
            return false;
        }

        const std::uint16_t pid =
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(
                    data[offset + 2] & 0x1F
                ) << 8) |
                data[offset + 3]
            );

        if (programNumber == 0)
        {
            // Program zero identifies a network PID,
            // not a PMT PID.

            result.hasNetworkPid = true;

            result.networkPid = pid;
        }
        else
        {
            result.programs.push_back(
                PatProgram{
                    programNumber,
                    pid
                }
            );
        }
    }

    table = std::move(result);

    return true;
}
