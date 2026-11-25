#include "UdpSocket.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    std::cout
        << "IPTV M10 UDP Socket Test\n\n";

    constexpr std::uint16_t TEST_PORT = 15004;

    UdpSocket socket;

    if (!socket.open(TEST_PORT))
    {
        std::cerr
            << "[FAIL] UDP socket open\n";

        return 1;
    }

    std::cout
        << "[PASS] UDP socket open\n";

    if (!socket.isOpen())
    {
        std::cerr
            << "[FAIL] UDP socket state\n";

        return 1;
    }

    std::cout
        << "[PASS] UDP socket state\n";

    if (!socket.setReceiveTimeout(200))
    {
        std::cerr
            << "[FAIL] Configure receive timeout\n";

        return 1;
    }

    std::cout
        << "[PASS] Configure receive timeout\n";

    if (!socket.setReceiveBufferSize(
            1024 * 1024
        ))
    {
        std::cerr
            << "[FAIL] Configure receive buffer\n";

        return 1;
    }

    std::cout
        << "[PASS] Configure receive buffer\n";

    std::array<std::uint8_t, 1024> buffer{};

    const auto start =
        std::chrono::steady_clock::now();

    const ssize_t timeoutResult =
        socket.receive(
            buffer.data(),
            buffer.size()
        );

    const int receiveError = errno;

    const auto end =
        std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(end - start).count();

    std::cout
        << "Receive timeout elapsed: "
        << elapsed << " ms\n";

    if (timeoutResult != -1 ||
        (receiveError != EAGAIN &&
         receiveError != EWOULDBLOCK))
    {
        std::cerr
            << "[FAIL] Receive timeout result\n";

        return 1;
    }

    std::cout
        << "[PASS] Receive timeout result\n";

    if (elapsed < 150 ||
        elapsed > 1500)
    {
        std::cerr
            << "[FAIL] Receive timeout duration\n";

        return 1;
    }

    std::cout
        << "[PASS] Receive timeout duration\n";

    const int senderFd =
        ::socket(
            AF_INET,
            SOCK_DGRAM | SOCK_CLOEXEC,
            0
        );

    if (senderFd < 0)
    {
        std::cerr
            << "[FAIL] Sender socket creation\n";

        return 1;
    }

    std::cout
        << "[PASS] Sender socket creation\n";

    sockaddr_in destination{};

    destination.sin_family = AF_INET;

    destination.sin_port =
        htons(TEST_PORT);

    destination.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    const std::array<std::uint8_t, 4>
        testData{
            0x11,
            0x22,
            0x33,
            0x44
        };

    const ssize_t sent =
        ::sendto(
            senderFd,
            testData.data(),
            testData.size(),
            0,
            reinterpret_cast<sockaddr*>(
                &destination
            ),
            sizeof(destination)
        );

    ::close(senderFd);

    if (sent !=
        static_cast<ssize_t>(
            testData.size()
        ))
    {
        std::cerr
            << "[FAIL] UDP datagram send\n";

        return 1;
    }

    std::cout
        << "[PASS] UDP datagram send\n";

    const ssize_t received =
        socket.receive(
            buffer.data(),
            buffer.size()
        );

    if (received !=
        static_cast<ssize_t>(
            testData.size()
        ))
    {
        std::cerr
            << "[FAIL] UDP datagram receive\n";

        return 1;
    }

    std::cout
        << "[PASS] UDP datagram receive\n";

    for (std::size_t i = 0;
         i < testData.size();
         ++i)
    {
        if (buffer[i] != testData[i])
        {
            std::cerr
                << "[FAIL] UDP payload validation\n";

            return 1;
        }
    }

    std::cout
        << "[PASS] UDP payload validation\n";

    socket.close();

    if (socket.isOpen())
    {
        std::cerr
            << "[FAIL] UDP socket close\n";

        return 1;
    }

    std::cout
        << "[PASS] UDP socket close\n";

    std::cout
        << "\nResult: PASS\n";

    return 0;
}
