#include "TsParser.hpp"

bool TsParser::parse(
    const std::uint8_t* data,
    std::size_t size,
    TsPacket& packet
)
{
    packet = TsPacket{};

    if (data == nullptr || size != PACKET_SIZE)
    {
        return false;
    }

    if (data[0] != SYNC_BYTE)
    {
        return false;
    }

    TsPacket result{};

    result.syncByte = data[0];

    // Byte 1

    result.transportError =
        (data[1] & 0x80) != 0;

    result.payloadUnitStart =
        (data[1] & 0x40) != 0;

    result.transportPriority =
        (data[1] & 0x20) != 0;

    // PID: 13 bits

    result.pid = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
            data[1] & 0x1F
        ) << 8) |
        data[2]
    );

    // Byte 3

    result.scramblingControl =
        (data[3] >> 6) & 0x03;

    result.adaptationFieldControl =
        (data[3] >> 4) & 0x03;

    result.continuityCounter =
        data[3] & 0x0F;

    // Adaptation Field Control = 0 is reserved

    if (result.adaptationFieldControl == 0)
    {
        return false;
    }

    result.hasAdaptationField =
        (result.adaptationFieldControl & 0x02) != 0;

    result.hasPayload =
        (result.adaptationFieldControl & 0x01) != 0;

    std::size_t offset = HEADER_SIZE;

    // Parse optional adaptation field

    if (result.hasAdaptationField)
    {
        result.adaptationFieldLength = data[offset];

        ++offset;

        const std::size_t adaptationLength =
            result.adaptationFieldLength;

        if (adaptationLength > size - offset)
        {
            return false;
        }

        // Adaptation-only packet must fill the packet

        if (!result.hasPayload &&
            adaptationLength != size - offset)
        {
            return false;
        }

        // Adaptation flags exist only when length > 0

        if (adaptationLength > 0)
        {
            const std::uint8_t flags = data[offset];

            result.discontinuityIndicator =
                (flags & 0x80) != 0;

            result.randomAccessIndicator =
                (flags & 0x40) != 0;

            result.hasPcr =
                (flags & 0x10) != 0;

            if (result.hasPcr)
            {
                // One flags byte and six PCR bytes

                if (adaptationLength < 7)
                {
                    return false;
                }

                const std::uint8_t* pcrData =
                    data + offset + 1;

                result.pcrBase =
                    (static_cast<std::uint64_t>(
                        pcrData[0]) << 25) |
                    (static_cast<std::uint64_t>(
                        pcrData[1]) << 17) |
                    (static_cast<std::uint64_t>(
                        pcrData[2]) << 9) |
                    (static_cast<std::uint64_t>(
                        pcrData[3]) << 1) |
                    (static_cast<std::uint64_t>(
                        pcrData[4]) >> 7);

                result.pcrExtension =
                    static_cast<std::uint16_t>(
                        ((pcrData[4] & 0x01) << 8) |
                        pcrData[5]
                    );

                if (result.pcrExtension >= 300)
                {
                    return false;
                }

                result.pcr =
                    result.pcrBase * 300ULL +
                    result.pcrExtension;
            }
        }

        offset += adaptationLength;
    }

    // Extract payload

    if (result.hasPayload)
    {
        if (offset >= size)
        {
            return false;
        }

        result.payload = data + offset;

        result.payloadSize = size - offset;
    }

    packet = result;

    return true;
}
