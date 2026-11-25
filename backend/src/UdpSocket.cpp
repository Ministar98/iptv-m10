#include "UdpSocket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>
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

    socketFd_ = ::socket(
        AF_INET,
        SOCK_DGRAM | SOCK_CLOEXEC,
        0
    );

    if (socketFd_ < 0)
    {
        return false;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    address.sin_port = htons(port);

    if (::bind(
            socketFd_,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            sizeof(address)
        ) < 0)
    {
        const int savedError = errno;

        close();

        errno = savedError;

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

bool UdpSocket::setReceiveTimeout(
    unsigned int timeoutMilliseconds
)
{
    if (socketFd_ < 0)
    {
        errno = EBADF;

        return false;
    }

    timeval timeout{};

    timeout.tv_sec =
        timeoutMilliseconds / 1000;

    timeout.tv_usec =
        (timeoutMilliseconds % 1000) * 1000;

    return ::setsockopt(
        socketFd_,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    ) == 0;
}

bool UdpSocket::setReceiveBufferSize(
    int bufferSize
)
{
    if (socketFd_ < 0)
    {
        errno = EBADF;

        return false;
    }

    if (bufferSize <= 0)
    {
        errno = EINVAL;

        return false;
    }

    return ::setsockopt(
        socketFd_,
        SOL_SOCKET,
        SO_RCVBUF,
        &bufferSize,
        sizeof(bufferSize)
    ) == 0;
}

ssize_t UdpSocket::receive(
    std::uint8_t* buffer,
    std::size_t bufferSize
)
{
    if (socketFd_ < 0 ||
        buffer == nullptr ||
        bufferSize == 0)
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
