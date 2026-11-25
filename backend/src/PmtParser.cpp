#include "PmtParser.hpp"
#include "PatParser.hpp"

#include <utility>

namespace
{
    bool validDescriptors(
        const std::uint8_t* data,
        std::size_t size
    )
    {
        std::size_t offset = 0;

        while (offset < size)
        {
            if (size - offset < 2)
            {
                return false;
            }

            const std::size_t descriptorLength =
                data[offset + 1];

            offset += 2;

            if (descriptorLength > size - offset)
            {
                return false;
            }

            offset += descriptorLength;
        }

        return true;
    }
}

StreamCategory PmtParser::classifyStream(
    std::uint8_t streamType
)
{
    switch (streamType)
    {
        case 0x01:
        case 0x02:
        case 0x10:
        case 0x1B:
        case 0x24:
            return StreamCategory::Video;

        case 0x03:
        case 0x04:
        case 0x0F:
        case 0x11:
            return StreamCategory::Audio;

        default:
            return StreamCategory::Other;
    }
}

const char* PmtParser::streamTypeName(
    std::uint8_t streamType
)
{
    switch (streamType)
    {
        case 0x01:
            return "MPEG-1 Video";

        case 0x02:
            return "MPEG-2 Video";

        case 0x03:
            return "MPEG-1 Audio";

        case 0x04:
            return "MPEG-2 Audio";

        case 0x06:
            return "Private PES Data";

        case 0x0F:
            return "AAC ADTS";

        case 0x10:
            return "MPEG-4 Video";

        case 0x11:
            return "AAC LATM";

        case 0x1B:
            return "H.264/AVC";

        case 0x24:
            return "H.265/HEVC";

        default:
            return "Unknown";
    }
}

const char* PmtParser::categoryName(
    StreamCategory category
)
{
    switch (category)
    {
        case StreamCategory::Video:
            return "Video";

        case StreamCategory::Audio:
            return "Audio";

        default:
            return "Other";
    }
}

bool PmtParser::parse(
    const std::uint8_t* data,
    std::size_t size,
    PmtTable& table
)
{
    table = PmtTable{};

    if (data == nullptr ||
        size < MIN_SECTION_SIZE)
    {
        return false;
    }

    if (data[0] != TABLE_ID)
    {
        return false;
    }

    // Section syntax indicator and reserved bits.

    if ((data[1] & 0xF0) != 0xB0)
    {
        return false;
    }

    const std::size_t sectionLength =
        (static_cast<std::size_t>(
            data[1] & 0x0F
        ) << 8) |
        data[2];

    if (sectionLength < 13 ||
        sectionLength > MAX_SECTION_LENGTH ||
        sectionLength + 3 != size)
    {
        return false;
    }

    // Reuse MPEG-2 CRC32 implementation.

    if (PatParser::calculateCrc32(data, size) != 0)
    {
        return false;
    }

    PmtTable result{};

    result.programNumber =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                data[3]
            ) << 8) |
            data[4]
        );

    if (result.programNumber == 0)
    {
        return false;
    }

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

    // PMT uses section number zero.

    if (result.sectionNumber != 0 ||
        result.lastSectionNumber != 0)
    {
        return false;
    }

    // PCR PID reserved bits.

    if ((data[8] & 0xE0) != 0xE0)
    {
        return false;
    }

    result.pcrPid =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                data[8] & 0x1F
            ) << 8) |
            data[9]
        );

    // Program information reserved bits.

    if ((data[10] & 0xF0) != 0xF0)
    {
        return false;
    }

    result.programInfoLength =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(
                data[10] & 0x0F
            ) << 8) |
            data[11]
        );

    const std::size_t entriesEnd =
        size - 4;

    std::size_t offset = 12;

    if (result.programInfoLength >
        entriesEnd - offset)
    {
        return false;
    }

    if (!validDescriptors(
            data + offset,
            result.programInfoLength))
    {
        return false;
    }

    result.programDescriptors.assign(
        data + offset,
        data + offset + result.programInfoLength
    );

    offset += result.programInfoLength;

    // Parse elementary streams.

    while (offset < entriesEnd)
    {
        if (entriesEnd - offset < 5)
        {
            return false;
        }

        PmtElementaryStream stream{};

        stream.streamType = data[offset];

        if ((data[offset + 1] & 0xE0) != 0xE0)
        {
            return false;
        }

        stream.elementaryPid =
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(
                    data[offset + 1] & 0x1F
                ) << 8) |
                data[offset + 2]
            );

        if ((data[offset + 3] & 0xF0) != 0xF0)
        {
            return false;
        }

        stream.esInfoLength =
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(
                    data[offset + 3] & 0x0F
                ) << 8) |
                data[offset + 4]
            );

        offset += 5;

        if (stream.esInfoLength >
            entriesEnd - offset)
        {
            return false;
        }

        if (!validDescriptors(
                data + offset,
                stream.esInfoLength))
        {
            return false;
        }

        stream.descriptors.assign(
            data + offset,
            data + offset + stream.esInfoLength
        );

        stream.category =
            classifyStream(stream.streamType);

        result.streams.push_back(
            std::move(stream)
        );

        offset +=
            result.streams.back().esInfoLength;
    }

    table = std::move(result);

    return true;
}
