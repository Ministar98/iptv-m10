#include "PsiSectionAssembler.hpp"

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

    TsPacket createPacket(
        const std::vector<std::uint8_t>& payload,
        bool pusi,
        std::uint8_t counter,
        std::uint16_t pid = 0
    )
    {
        TsPacket packet{};

        packet.pid = pid;

        packet.hasPayload = true;

        packet.payloadUnitStart = pusi;

        packet.continuityCounter = counter;

        packet.payload = payload.data();

        packet.payloadSize = payload.size();

        return packet;
    }

    const PsiSection SECTION = {
        0x00, 0xB0, 0x05,
        0x11, 0x22, 0x33, 0x44, 0x55
    };

    void testCompleteSection()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> payload = {
            0x00,
            0x00, 0xB0, 0x05,
            0x11, 0x22, 0x33, 0x44, 0x55
        };

        const auto result =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            result.size() == 1 &&
            result[0] == SECTION,
            "Complete PSI section"
        );
    }

    void testSplitSection()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00,
            0x00, 0xB0, 0x05, 0x11
        };

        const std::vector<std::uint8_t> second = {
            0x22, 0x33, 0x44, 0x55
        };

        const auto a =
            assembler.push(
                createPacket(first, true, 0)
            );

        const auto b =
            assembler.push(
                createPacket(second, false, 1)
            );

        check(
            a.empty() &&
            b.size() == 1 &&
            b[0] == SECTION,
            "Section split across two packets"
        );
    }

    void testSplitHeader()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00, 0x00, 0xB0
        };

        const std::vector<std::uint8_t> second = {
            0x05, 0x11, 0x22,
            0x33, 0x44, 0x55
        };

        const auto a =
            assembler.push(
                createPacket(first, true, 0)
            );

        const auto b =
            assembler.push(
                createPacket(second, false, 1)
            );

        check(
            a.empty() &&
            b.size() == 1 &&
            b[0] == SECTION,
            "Split PSI section header"
        );
    }

    void testMultipleSections()
    {
        PsiSectionAssembler assembler(0);

        std::vector<std::uint8_t> payload = {
            0x00
        };

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        const auto result =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            result.size() == 2 &&
            result[0] == SECTION &&
            result[1] == SECTION,
            "Multiple sections in one packet"
        );
    }

    void testPointerField()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00,
            0x00, 0xB0, 0x05, 0x11
        };

        std::vector<std::uint8_t> second = {
            0x04,
            0x22, 0x33, 0x44, 0x55
        };

        second.insert(
            second.end(),
            SECTION.begin(),
            SECTION.end()
        );

        const auto a =
            assembler.push(
                createPacket(first, true, 0)
            );

        const auto b =
            assembler.push(
                createPacket(second, true, 1)
            );

        check(
            a.empty() &&
            b.size() == 2 &&
            b[0] == SECTION &&
            b[1] == SECTION,
            "Pointer completes previous section"
        );
    }

    void testStuffing()
    {
        PsiSectionAssembler assembler(0);

        std::vector<std::uint8_t> payload = {
            0x00
        };

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        payload.insert(
            payload.end(), 10, 0xFF
        );

        const auto result =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            result.size() == 1 &&
            result[0] == SECTION,
            "Stuffing bytes ignored"
        );
    }

    void testWrongPid()
    {
        PsiSectionAssembler assembler(0);

        std::vector<std::uint8_t> payload = {
            0x00
        };

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        const auto result =
            assembler.push(
                createPacket(
                    payload, true, 0, 256
                )
            );

        check(
            result.empty(),
            "Unrelated PID ignored"
        );
    }

    void testContinuityLoss()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00,
            0x00, 0xB0, 0x05, 0x11
        };

        const std::vector<std::uint8_t> second = {
            0x22, 0x33, 0x44, 0x55
        };

        assembler.push(
            createPacket(first, true, 0)
        );

        const auto result =
            assembler.push(
                createPacket(second, false, 2)
            );

        check(
            result.empty(),
            "Incomplete section discarded after loss"
        );
    }

    void testCounterWrap()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00,
            0x00, 0xB0, 0x05, 0x11
        };

        const std::vector<std::uint8_t> second = {
            0x22, 0x33, 0x44, 0x55
        };

        assembler.push(
            createPacket(first, true, 15)
        );

        const auto result =
            assembler.push(
                createPacket(second, false, 0)
            );

        check(
            result.size() == 1 &&
            result[0] == SECTION,
            "Continuity counter wraparound"
        );
    }

    void testDuplicate()
    {
        PsiSectionAssembler assembler(0);

        std::vector<std::uint8_t> payload = {
            0x00
        };

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        const auto a =
            assembler.push(
                createPacket(payload, true, 0)
            );

        const auto b =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            a.size() == 1 &&
            b.empty(),
            "Duplicate payload counter ignored"
        );
    }

    void testInvalidPointer()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> payload = {
            0x20, 0x00, 0xB0
        };

        const auto result =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            result.empty(),
            "Invalid pointer field"
        );
    }

    void testInvalidSectionLength()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> payload = {
            0x00, 0x00, 0xBF, 0xFF
        };

        const auto result =
            assembler.push(
                createPacket(payload, true, 0)
            );

        check(
            result.empty(),
            "Invalid section length"
        );
    }

    void testTransportError()
    {
        PsiSectionAssembler assembler(0);

        std::vector<std::uint8_t> payload = {
            0x00
        };

        payload.insert(
            payload.end(),
            SECTION.begin(),
            SECTION.end()
        );

        auto packet =
            createPacket(payload, true, 0);

        packet.transportError = true;

        const auto result =
            assembler.push(packet);

        check(
            result.empty(),
            "Transport error packet discarded"
        );
    }

    void testReset()
    {
        PsiSectionAssembler assembler(0);

        const std::vector<std::uint8_t> first = {
            0x00,
            0x00, 0xB0, 0x05, 0x11
        };

        const std::vector<std::uint8_t> second = {
            0x22, 0x33, 0x44, 0x55
        };

        assembler.push(
            createPacket(first, true, 0)
        );

        assembler.reset();

        const auto result =
            assembler.push(
                createPacket(second, false, 1)
            );

        check(
            result.empty(),
            "Assembler reset"
        );
    }
}

int main()
{
    std::cout << "IPTV M10 PSI Assembler Tests\n\n";

    testCompleteSection();
    testSplitSection();
    testSplitHeader();
    testMultipleSections();
    testPointerField();
    testStuffing();
    testWrongPid();
    testContinuityLoss();
    testCounterWrap();
    testDuplicate();
    testInvalidPointer();
    testInvalidSectionLength();
    testTransportError();
    testReset();

    std::cout << "\nPassed: "
              << passedTests << '\n';

    std::cout << "Failed: "
              << failedTests << '\n';

    return failedTests == 0 ? 0 : 1;
}
