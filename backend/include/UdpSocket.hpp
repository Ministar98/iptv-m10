#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(std::uint16_t port);
    void close();

    ssize_t receive(std::uint8_t* buffer, std::size_t bufferSize);

    bool isOpen() const;

private:
    int socketFd_;
};
