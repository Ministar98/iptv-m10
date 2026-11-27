#pragma once

#include "ApiRequestHandler.hpp"
#include "EventQueue.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

class HttpServer
{
public:
    HttpServer(
        const ApiRequestHandler& handler,
        EventQueue& events,
        std::uint16_t port = 8080
    );

    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start();
    void stop();

    bool isRunning() const noexcept;

private:
    using Tcp = boost::asio::ip::tcp;

    const ApiRequestHandler& handler_;
    EventQueue& events_;

    const std::uint16_t port_;

    const std::filesystem::path hlsDirectory_ =
        "runtime/hls";

    boost::asio::io_context ioContext_;
    Tcp::acceptor acceptor_;

    boost::asio::thread_pool workers_{4};

    std::thread acceptThread_;

    std::atomic<bool> running_{false};
    std::atomic<bool> websocketConnected_{false};

    std::atomic<std::uint64_t> requestCounter_{0};
    std::atomic<unsigned int> pendingConnections_{0};

    bool started_ = false;

    void acceptLoop();

    void handleConnection(Tcp::socket socket);

    void handleWebSocket(
        Tcp::socket socket,
        boost::beast::http::request<
            boost::beast::http::string_body
        > request
    );

    void handleHls(
        Tcp::socket& socket,
        const boost::beast::http::request<
            boost::beast::http::string_body
        >& request
    );
};
