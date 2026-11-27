#include "HttpServer.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <utility>
#include <algorithm>
#include <charconv>
#include <string_view>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

HttpServer::HttpServer(
    const ApiRequestHandler& handler,
    EventQueue& events,
    std::uint16_t port
)
    : handler_(handler),
      events_(events),
      port_(port),
      acceptor_(ioContext_)
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start()
{
    if (started_)
    {
        return false;
    }

    started_ = true;

    try
    {
        const Tcp::endpoint endpoint(
            asio::ip::make_address("127.0.0.1"),
            port_
        );

        acceptor_.open(endpoint.protocol());

        acceptor_.set_option(
            asio::socket_base::reuse_address(true)
        );

        acceptor_.bind(endpoint);
        acceptor_.listen(32);
        acceptor_.non_blocking(true);

        running_.store(true);

        acceptThread_ = std::thread(
            &HttpServer::acceptLoop,
            this
        );

        std::cout
            << "[INFO] HTTP server listening on 127.0.0.1:"
            << port_ << '\n';

        return true;
    }
    catch (const std::exception& error)
    {
        running_.store(false);

        boost::system::error_code ec;

        acceptor_.close(ec);

        std::cerr
            << "[ERROR] HTTP server: "
            << error.what() << '\n';

        return false;
    }
}

void HttpServer::stop()
{
    if (!started_)
    {
        return;
    }

    running_.store(false);

    if (acceptThread_.joinable())
    {
        acceptThread_.join();
    }

    boost::system::error_code ec;

    acceptor_.close(ec);

    workers_.join();

    std::cout << "[INFO] HTTP server stopped\n";

    started_ = false;
}

bool HttpServer::isRunning() const noexcept
{
    return running_.load();
}

void HttpServer::acceptLoop()
{
    while (running_.load())
    {
        boost::system::error_code ec;

        Tcp::socket socket(ioContext_);

        acceptor_.accept(socket, ec);

        if (ec == asio::error::would_block ||
            ec == asio::error::try_again)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(20)
            );

            continue;
        }

        if (ec)
        {
            std::cerr
                << "[ERROR] HTTP accept: "
                << ec.message() << '\n';

            running_.store(false);

            break;
        }

        if (pendingConnections_.load() >= 16)
        {
            socket.close(ec);

            continue;
        }

        pendingConnections_.fetch_add(1);

        asio::post(
            workers_,
            [this, socket = std::move(socket)]() mutable
            {
                try
                {
                    if (running_.load())
                    {
                        handleConnection(
                            std::move(socket)
                        );
                    }
                }
                catch (const std::exception& error)
                {
                    std::cerr
                        << "[WARNING] HTTP connection: "
                        << error.what() << '\n';
                }

                pendingConnections_.fetch_sub(1);
            }
        );
    }
}

void HttpServer::handleConnection(Tcp::socket socket)
{
    socket.non_blocking(false);

    timeval timeout{5, 0};

    if (::setsockopt(
            socket.native_handle(),
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0 ||
        ::setsockopt(
            socket.native_handle(),
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0)
    {
        return;
    }

    beast::flat_buffer buffer;

    http::request_parser<http::string_body> parser;

    parser.header_limit(8192);
    parser.body_limit(16384);

    boost::system::error_code ec;

    http::read(socket, buffer, parser, ec);

    if (ec)
    {
        return;
    }

    auto request = parser.release();

	if (request.target().substr(0, 5) == "/hls/")
	{
	    handleHls(socket, request);

	    return;
	}

	if (request.target() == "/api/v1/events" &&
	    beast::websocket::is_upgrade(request))
	{
	    handleWebSocket(
	        std::move(socket),
	        std::move(request)
	    );

	    return;
	}

    ApiRequest apiRequest;

    apiRequest.method = std::string(
        request.method_string()
    );

    apiRequest.target = std::string(
        request.target()
    );

    apiRequest.requestId =
        "http-" +
        std::to_string(
            requestCounter_.fetch_add(1) + 1
        );

    apiRequest.body = std::move(request.body());

    const auto apiResponse =
        handler_.handle(apiRequest);

    http::response<http::string_body> response{
        static_cast<http::status>(
            apiResponse.statusCode
        ),
        request.version()
    };

    response.set(
        http::field::server,
        "IPTV-M10"
    );

    response.set(
        http::field::content_type,
        apiResponse.contentType
    );

    response.set(
        http::field::cache_control,
        "no-store"
    );

    if (!apiResponse.allowedMethod.empty())
    {
        response.set(
            http::field::allow,
            apiResponse.allowedMethod
        );
    }

    if (apiResponse.statusCode == 426)
    {
        response.set(
            http::field::upgrade,
            "websocket"
        );
    }

    response.keep_alive(false);

    response.body() = apiResponse.body.dump();

    response.prepare_payload();

    http::write(socket, response, ec);

    socket.shutdown(
        Tcp::socket::shutdown_both,
        ec
    );
}
void HttpServer::handleWebSocket(
    Tcp::socket socket,
    http::request<http::string_body> request
)
{
    namespace websocket = beast::websocket;

    bool expected = false;

    if (!websocketConnected_.compare_exchange_strong(
            expected,
            true))
    {
        http::response<http::string_body> response{
            http::status::service_unavailable,
            request.version()
        };

        response.body() =
            "WebSocket client already connected";

        response.keep_alive(false);
        response.prepare_payload();

        boost::system::error_code ec;

        http::write(socket, response, ec);

        return;
    }

    struct ConnectionGuard
    {
        std::atomic<bool>& connected;

        ~ConnectionGuard()
        {
            connected.store(false);
        }
    };

    ConnectionGuard guard{websocketConnected_};

    websocket::stream<Tcp::socket> ws(
        std::move(socket)
    );

    boost::system::error_code ec;

    ws.accept(request, ec);

    if (ec)
    {
        return;
    }

    ws.text(true);

    std::cout
        << "[INFO] WebSocket client connected\n";

    beast::flat_buffer inputBuffer;

    ws.read_message_max(16384);

    bool readPending = false;

    const auto startRead = [&]()
    {
        readPending = true;

        ws.async_read(
            inputBuffer,
            [&](boost::system::error_code error,
                std::size_t)
            {
                ec = error;

                inputBuffer.consume(
                    inputBuffer.size()
                );

                readPending = false;
            }
        );
    };

    startRead();

    while (running_.load() && !ec)
    {
        if (const auto event = events_.tryPop())
        {
            const std::string message = event->dump();

            boost::system::error_code writeError;

            ws.write(
                asio::buffer(message),
                writeError
            );

            if (writeError)
            {
                break;
            }
        }

        // Obrađujemo asinhrono čitanje isključivo
        // u ovoj radnoj niti.
        //
        // Poseban io_context za WebSocket bi bio
        // potreban za više paralelnih klijenata.

        ioContext_.restart();

        ioContext_.poll();

        if (ec)
        {
            break;
        }

        if (!readPending)
        {
            startRead();
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(20)
        );
    }

    boost::system::error_code closeError;

    ws.next_layer().cancel(closeError);
    ws.next_layer().close(closeError);

    while (readPending)
    {
        ioContext_.restart();
        ioContext_.poll();
    }

    std::cout
        << "[INFO] WebSocket client disconnected\n";
}
void HttpServer::handleHls(
    Tcp::socket& socket,
    const http::request<http::string_body>& request
)
{
    boost::system::error_code ec;

    const auto sendError =
        [&](http::status status,
            const std::string& message)
    {
        http::response<http::string_body> response{
            status,
            request.version()
        };

        response.set(
            http::field::server,
            "IPTV-M10"
        );

        response.set(
            http::field::content_type,
            "text/plain"
        );

        response.set(
            http::field::cache_control,
            "no-store"
        );

        if (status == http::status::method_not_allowed)
        {
            response.set(http::field::allow, "GET");
        }

        response.keep_alive(false);
        response.body() = message;
        response.prepare_payload();

        http::write(socket, response, ec);
    };

    if (request.method() != http::verb::get)
    {
        sendError(
            http::status::method_not_allowed,
            "Only GET is supported"
        );

        return;
    }

    const std::string target(request.target());

    const std::string_view path(target);

    const auto separator = path.find('/', 5);

    if (separator == std::string_view::npos)
    {
        sendError(
            http::status::bad_request,
            "Invalid HLS path"
        );

        return;
    }

    const auto generationText =
        path.substr(5, separator - 5);

    std::uint64_t generationId = 0;

    const auto parsed = std::from_chars(
        generationText.data(),
        generationText.data() + generationText.size(),
        generationId
    );

    if (parsed.ec != std::errc{} ||
        parsed.ptr !=
            generationText.data() + generationText.size() ||
        generationId == 0)
    {
        sendError(
            http::status::bad_request,
            "Invalid generation ID"
        );

        return;
    }

    const auto filename =
        path.substr(separator + 1);

    const bool playlist =
        filename == "index.m3u8";

    bool segment = false;

    constexpr std::string_view prefix = "segment_";
    constexpr std::string_view suffix = ".ts";

    if (filename.size() >
            prefix.size() + suffix.size() &&
        filename.substr(0, prefix.size()) == prefix &&
        filename.substr(
            filename.size() - suffix.size()
        ) == suffix)
    {
        const auto digits = filename.substr(
            prefix.size(),
            filename.size() -
                prefix.size() -
                suffix.size()
        );

        segment = std::all_of(
            digits.begin(),
            digits.end(),
            [](char value)
            {
                return value >= '0' && value <= '9';
            }
        );
    }

    if (!playlist && !segment)
    {
        sendError(
            http::status::not_found,
            "HLS resource not found"
        );

        return;
    }

    const auto filePath =
        hlsDirectory_ /
        std::to_string(generationId) /
        std::string(filename);

    http::file_body::value_type file;

    file.open(
        filePath.c_str(),
        beast::file_mode::scan,
        ec
    );

    if (ec)
    {
        sendError(
            http::status::not_found,
            "HLS resource unavailable"
        );

        return;
    }

    const auto fileSize = file.size();

    http::response<http::file_body> response{
        http::status::ok,
        request.version()
    };

    response.set(
        http::field::server,
        "IPTV-M10"
    );

    response.set(
        http::field::content_type,
        playlist
            ? "application/vnd.apple.mpegurl"
            : "video/mp2t"
    );

    response.set(
        http::field::cache_control,
        "no-store"
    );

    response.set(
        http::field::access_control_allow_origin,
        "*"
    );

    response.keep_alive(false);

    response.body() = std::move(file);
    response.content_length(fileSize);

    http::write(socket, response, ec);

    socket.shutdown(
        Tcp::socket::shutdown_both,
        ec
    );
}
