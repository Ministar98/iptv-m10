#include "RtpParser.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    int failedTests = 0;
    int passedTests = 0;

    void check(bool condition, const char* testName)
    {
        if (condition)
        {
            ++passedTests;

            std::cout << "[PASS] "
                      << testName << '\n';
        }
        else
        {
            ++failedTests;

            std::cerr << "[FAIL] "
                      << testName << '\n';
        }
    }

    std::vector<std::uint8_t> createPacket()
    {
        std::vector<std::uint8_t> data(
            12 + 188,
            0
        );

        // Version = 2
        data[0] = 0x80;

        // Payload Type = 33
        data[1] = 0x21;

        // Sequence Number = 0x1234
        data[2] = 0x12;
        data[3] = 0x34;

        // Timestamp = 0x01020304
        data[4] = 0x01;
        data[5] = 0x02;
        data[6] = 0x03;
        data[7] = 0x04;

        // SSRC = 0x11223344
        data[8]  = 0x11;
        data[9]  = 0x22;
        data[10] = 0x33;
        data[11] = 0x44;

        // MPEG-TS sync byte
        data[12] = 0x47;

        return data;
    }

    void testValidPacket()
    {
        const auto data = createPacket();

        RtpPacket packet{};

        const bool valid = RtpParser::parse(
            data.data(),
            data.size(),
            packet
        );

        check(
            valid &&
            packet.version == 2 &&
            packet.payloadType == 33 &&
            packet.sequenceNumber == 0x1234 &&
            packet.timestamp == 0x01020304 &&
            packet.ssrc == 0x11223344 &&
            packet.headerSize == 12 &&
            packet.payloadSize == 188 &&
            packet.payload == data.data() + 12 &&
            packet.payload[0] == 0x47,
            "Valid RTP packet"
        );
    }

    void testInvalidVersion()
    {
        auto data = createPacket();

        data[0] = 0x40;

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Invalid RTP version"
        );
    }

    void testShortPacket()
    {
        const std::vector<std::uint8_t> data(8, 0);

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Packet shorter than RTP header"
        );
    }

    void testNullPointer()
    {
        RtpPacket packet{};

        check(
            !RtpParser::parse(
                nullptr,
                100,
                packet
            ),
            "Null input pointer"
        );
    }

    void testCsrc()
    {
        auto data = createPacket();

        data[0] = 0x81;

        data.insert(
            data.begin() + 12,
            {0xAA, 0xBB, 0xCC, 0xDD}
        );

        RtpPacket packet{};

        check(
            RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ) &&
            packet.csrcCount == 1 &&
            packet.headerSize == 16 &&
            packet.payloadSize == 188 &&
            packet.payload[0] == 0x47,
            "RTP packet with CSRC"
        );
    }

    void testInvalidCsrc()
    {
        auto data = createPacket();

        data[0] = 0x8F;

        data.resize(12);

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Truncated CSRC list"
        );
    }

    void testExtension()
    {
        auto data = createPacket();

        data[0] = 0x90;

        data.insert(
            data.begin() + 12,
            {
                0xBE, 0xDE,
                0x00, 0x01,
                0x11, 0x22, 0x33, 0x44
            }
        );

        RtpPacket packet{};

        check(
            RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ) &&
            packet.extension &&
            packet.headerSize == 20 &&
            packet.payloadSize == 188 &&
            packet.payload[0] == 0x47,
            "RTP header extension"
        );
    }

    void testInvalidExtension()
    {
        auto data = createPacket();

        data[0] = 0x90;

        data.resize(16);

        data[14] = 0x00;
        data[15] = 0x10;

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Truncated RTP extension"
        );
    }

    void testPadding()
    {
        auto data = createPacket();

        data[0] = 0xA0;

        data.insert(
            data.end(),
            {0x00, 0x00, 0x00, 0x04}
        );

        RtpPacket packet{};

        check(
            RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ) &&
            packet.padding &&
            packet.payloadSize == 188,
            "RTP packet with padding"
        );
    }

    void testInvalidPadding()
    {
        auto data = createPacket();

        data[0] = 0xA0;

        data.back() = 255;

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Invalid RTP padding size"
        );
    }

    void testMarker()
    {
        auto data = createPacket();

        data[1] = 0xA1;

        RtpPacket packet{};

        check(
            RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ) &&
            packet.marker &&
            packet.payloadType == 33,
            "RTP marker bit"
        );
    }

    void testZeroPadding()
    {
        auto data = createPacket();

        data[0] = 0xA0;

        data.back() = 0;

        RtpPacket packet{};

        check(
            !RtpParser::parse(
                data.data(),
                data.size(),
                packet
            ),
            "Invalid zero padding"
        );
    }
}

int main()
{
    std::cout << "IPTV M10 RTP Parser Tests\n\n";

    testValidPacket();
    testInvalidVersion();
    testShortPacket();
    testNullPointer();
    testCsrc();
    testInvalidCsrc();
    testExtension();
    testInvalidExtension();
    testPadding();
    testInvalidPadding();
    testMarker();
    testZeroPadding();

    std::cout << "\nPassed: " << passedTests << '\n';

    std::cout << "Failed: " << failedTests << '\n';

    return failedTests == 0 ? 0 : 1;
}
