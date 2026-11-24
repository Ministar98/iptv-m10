#include "UdpSocket.hpp"

#include <array>
#include <cstdint>
#include <iostream>

int main()
{
    constexpr std::uint16_t RTP_PORT = 5004;

    UdpSocket socket;

    if (!socket.open(RTP_PORT))
    {
        std::cerr << "Failed to open UDP port "
                  << RTP_PORT << '\n';

        return 1;
    }

    std::cout << "IPTV M10 UDP Receiver\n";
    std::cout << "Listening on UDP port "
              << RTP_PORT << '\n';

    std::array<std::uint8_t, 2048> buffer{};

    for (int i = 0; i < 10; ++i)
    {
        ssize_t receivedBytes =
            socket.receive(buffer.data(), buffer.size());

        if (receivedBytes < 0)
        {
            std::cerr << "UDP receive error\n";
            return 1;
        }

        std::cout << "Received UDP datagram: "
                  << receivedBytes
                  << " bytes\n";
    }

    std::cout << "UDP reception test completed.\n";

    return 0;
}
