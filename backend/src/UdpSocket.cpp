#include "UdpSocket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

UdpSocket::UdpSocket()
    : socketFd_(-1)
{
}

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::open(std::uint16_t port)
{
    close();

    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);

    if (socketFd_ < 0)
    {
        return false;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(
            socketFd_,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        close();
        return false;
    }

    return true;
}

void UdpSocket::close()
{
    if (socketFd_ >= 0)
    {
        ::close(socketFd_);
        socketFd_ = -1;
    }
}

ssize_t UdpSocket::receive(
    std::uint8_t* buffer,
    std::size_t bufferSize)
{
    if (socketFd_ < 0 || buffer == nullptr || bufferSize == 0)
    {
        errno = EINVAL;
        return -1;
    }

    return ::recvfrom(
        socketFd_,
        buffer,
        bufferSize,
        0,
        nullptr,
        nullptr
    );
}

bool UdpSocket::isOpen() const
{
    return socketFd_ >= 0;
}
