#include "RtpParser.hpp"

namespace
{
    std::uint16_t readUint16(
        const std::uint8_t* data
    )
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[0]) << 8) |
             static_cast<std::uint16_t>(data[1])
        );
    }

    std::uint32_t readUint32(
        const std::uint8_t* data
    )
    {
        return
            (static_cast<std::uint32_t>(data[0]) << 24) |
            (static_cast<std::uint32_t>(data[1]) << 16) |
            (static_cast<std::uint32_t>(data[2]) << 8)  |
             static_cast<std::uint32_t>(data[3]);
    }
}

bool RtpParser::parse(
    const std::uint8_t* data,
    std::size_t size,
    RtpPacket& packet
)
{
    packet = RtpPacket{};

    if (data == nullptr || size < MIN_HEADER_SIZE)
    {
        return false;
    }

    RtpPacket result{};

    // First byte
    result.version =
        (data[0] >> 6) & 0x03;

    result.padding =
        (data[0] & 0x20) != 0;

    result.extension =
        (data[0] & 0x10) != 0;

    result.csrcCount =
        data[0] & 0x0F;

    // Second byte
    result.marker =
        (data[1] & 0x80) != 0;

    result.payloadType =
        data[1] & 0x7F;

    // Validate RTP version
    if (result.version != RTP_VERSION)
    {
        return false;
    }

    // Fixed header fields
    result.sequenceNumber =
        readUint16(data + 2);

    result.timestamp =
        readUint32(data + 4);

    result.ssrc =
        readUint32(data + 8);

    // Fixed header and optional CSRC list
    std::size_t headerSize =
        MIN_HEADER_SIZE +
        static_cast<std::size_t>(result.csrcCount) * 4;

    if (headerSize > size)
    {
        return false;
    }

    // Optional RTP header extension
    if (result.extension)
    {
        constexpr std::size_t EXTENSION_HEADER_SIZE = 4;

        if (size - headerSize < EXTENSION_HEADER_SIZE)
        {
            return false;
        }

        const std::uint16_t extensionLength =
            readUint16(data + headerSize + 2);

        const std::size_t extensionDataSize =
            static_cast<std::size_t>(extensionLength) * 4;

        headerSize += EXTENSION_HEADER_SIZE;

        if (extensionDataSize > size - headerSize)
        {
            return false;
        }

        headerSize += extensionDataSize;
    }

    std::size_t payloadSize = size - headerSize;

    // Optional RTP padding
    if (result.padding)
    {
        if (payloadSize == 0)
        {
            return false;
        }

        const std::uint8_t paddingSize =
            data[size - 1];

        if (paddingSize == 0 ||
            paddingSize > payloadSize)
        {
            return false;
        }

        payloadSize -= paddingSize;
    }

    result.headerSize = headerSize;

    result.payload = data + headerSize;

    result.payloadSize = payloadSize;

    packet = result;

    return true;
}
