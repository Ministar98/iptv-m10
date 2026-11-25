#include "TsParser.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{
    using PacketData =
        std::array<std::uint8_t, TsParser::PACKET_SIZE>;

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

    PacketData createPacket()
    {
        PacketData data{};

        data.fill(0xFF);

        data[0] = 0x47;

        // PUSI = 1, PID = 0

        data[1] = 0x40;
        data[2] = 0x00;

        // Payload only, CC = 5

        data[3] = 0x15;

        data[4] = 0x00;

        return data;
    }

    void testValidPacket()
    {
        const auto data = createPacket();

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.syncByte == 0x47 &&
            packet.pid == 0 &&
            packet.payloadUnitStart &&
            packet.hasPayload &&
            !packet.hasAdaptationField &&
            packet.continuityCounter == 5 &&
            packet.payloadSize == 184 &&
            packet.payload == data.data() + 4,
            "Valid MPEG-TS packet"
        );
    }

    void testInvalidSyncByte()
    {
        auto data = createPacket();

        data[0] = 0x00;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Invalid sync byte"
        );
    }

    void testInvalidSize()
    {
        const auto data = createPacket();

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), 187, packet
            ),
            "Invalid packet size"
        );
    }

    void testNullPointer()
    {
        TsPacket packet{};

        check(
            !TsParser::parse(nullptr, 188, packet),
            "Null input pointer"
        );
    }

    void testPid()
    {
        auto data = createPacket();

        data[1] = 0x50;
        data[2] = 0x00;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.pid == 0x1000 &&
            packet.payloadUnitStart,
            "13-bit PID extraction"
        );
    }

    void testTransportError()
    {
        auto data = createPacket();

        data[1] |= 0x80;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.transportError,
            "Transport error indicator"
        );
    }

    void testTransportPriority()
    {
        auto data = createPacket();

        data[1] |= 0x20;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.transportPriority,
            "Transport priority"
        );
    }

    void testReservedAdaptationControl()
    {
        auto data = createPacket();

        data[3] = 0x05;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Reserved adaptation field control"
        );
    }

    void testAdaptationAndPayload()
    {
        auto data = createPacket();

        data[3] = 0x35;

        // One flags byte

        data[4] = 1;
        data[5] = 0xC0;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.hasAdaptationField &&
            packet.hasPayload &&
            packet.discontinuityIndicator &&
            packet.randomAccessIndicator &&
            packet.payloadSize == 182 &&
            packet.payload == data.data() + 6,
            "Adaptation field and payload"
        );
    }

    void testAdaptationOnly()
    {
        auto data = createPacket();

        data[3] = 0x25;

        data[4] = 183;
        data[5] = 0x00;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.hasAdaptationField &&
            !packet.hasPayload &&
            packet.payload == nullptr &&
            packet.payloadSize == 0,
            "Adaptation-only packet"
        );
    }

    void testInvalidAdaptationOnly()
    {
        auto data = createPacket();

        data[3] = 0x25;

        data[4] = 10;
        data[5] = 0x00;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Invalid adaptation-only length"
        );
    }

    void testInvalidAdaptationLength()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 255;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Adaptation field exceeds packet size"
        );
    }

    void testZeroAdaptationLength()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 0;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.hasAdaptationField &&
            packet.payloadSize == 183 &&
            !packet.hasPcr,
            "Zero-length adaptation field"
        );
    }

    void testPcr()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 7;

        // PCR flag

        data[5] = 0x10;

        // PCR base = 1
        // PCR extension = 42

        data[6]  = 0x00;
        data[7]  = 0x00;
        data[8]  = 0x00;
        data[9]  = 0x00;
        data[10] = 0xFE;
        data[11] = 0x2A;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.hasPcr &&
            packet.pcrBase == 1 &&
            packet.pcrExtension == 42 &&
            packet.pcr == 342 &&
            packet.payloadSize == 176,
            "PCR extraction"
        );
    }

    void testTruncatedPcr()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 1;
        data[5] = 0x10;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Truncated PCR field"
        );
    }

    void testInvalidPcrExtension()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 7;
        data[5] = 0x10;

        data[6] = 0;
        data[7] = 0;
        data[8] = 0;
        data[9] = 0;

        data[10] = 0x7F;
        data[11] = 0xFF;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Invalid PCR extension"
        );
    }

    void testMissingPayload()
    {
        auto data = createPacket();

        data[3] = 0x35;

        data[4] = 183;
        data[5] = 0;

        TsPacket packet{};

        check(
            !TsParser::parse(
                data.data(), data.size(), packet
            ),
            "Payload flag without payload"
        );
    }

    void testNullPid()
    {
        auto data = createPacket();

        data[1] = 0x1F;
        data[2] = 0xFF;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.pid == TsParser::NULL_PID,
            "Null packet PID"
        );
    }

    void testScramblingControl()
    {
        auto data = createPacket();

        data[3] = 0x95;

        TsPacket packet{};

        check(
            TsParser::parse(
                data.data(), data.size(), packet
            ) &&
            packet.scramblingControl == 2 &&
            packet.continuityCounter == 5,
            "Scrambling control extraction"
        );
    }
}

int main()
{
    std::cout << "IPTV M10 MPEG-TS Parser Tests\n\n";

    testValidPacket();
    testInvalidSyncByte();
    testInvalidSize();
    testNullPointer();
    testPid();
    testTransportError();
    testTransportPriority();
    testReservedAdaptationControl();
    testAdaptationAndPayload();
    testAdaptationOnly();
    testInvalidAdaptationOnly();
    testInvalidAdaptationLength();
    testZeroAdaptationLength();
    testPcr();
    testTruncatedPcr();
    testInvalidPcrExtension();
    testMissingPayload();
    testNullPid();
    testScramblingControl();

    std::cout << "\nPassed: " << passedTests << '\n';

    std::cout << "Failed: " << failedTests << '\n';

    return failedTests == 0 ? 0 : 1;
}
