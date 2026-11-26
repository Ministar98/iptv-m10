#include "HlsSegmenter.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <system_error>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

HlsSegmenter::HlsSegmenter()
    : inputFd_(-1),
      processId_(-1)
{
}

HlsSegmenter::~HlsSegmenter()
{
    stop();
}

bool HlsSegmenter::start(
    const HlsConfig& config
)
{
    if (isRunning())
    {
        std::cerr
            << "HLS segmenter already running\n";

        return false;
    }

    stop();

    const std::filesystem::path playlistName =
        config.playlistFilename;

    if (config.outputDirectory.empty() ||
        config.segmentDurationSeconds == 0 ||
        config.playlistSize == 0 ||
        config.deleteThreshold == 0 ||
        playlistName.empty() ||
        playlistName.has_parent_path() ||
        playlistName.extension() != ".m3u8")
    {
        std::cerr
            << "Invalid HLS configuration\n";

        return false;
    }

    std::error_code error;

    std::filesystem::create_directories(
        config.outputDirectory,
        error
    );

    if (error)
    {
        std::cerr
            << "Failed to create HLS directory: "
            << error.message() << '\n';

        return false;
    }

    const std::string playlistPath =
        (config.outputDirectory /
         playlistName).string();

    const std::string segmentPath =
        (config.outputDirectory /
         "segment_%06d.ts").string();

    const std::string segmentDuration =
        std::to_string(
            config.segmentDurationSeconds
        );

    const std::string playlistSize =
        std::to_string(
            config.playlistSize
        );

    const std::string deleteThreshold =
        std::to_string(
            config.deleteThreshold
        );

    std::string hlsFlags =
        "independent_segments+temp_file";

    if (config.deleteSegments)
    {
        hlsFlags =
            "delete_segments+" + hlsFlags;
    }

    int pipeFds[2];

    if (::pipe2(pipeFds, O_CLOEXEC) < 0)
    {
        std::cerr
            << "Failed to create FFmpeg pipe\n";

        return false;
    }

    const int flags =
        ::fcntl(pipeFds[1], F_GETFL);

    if (flags < 0 ||
        ::fcntl(
            pipeFds[1],
            F_SETFL,
            flags | O_NONBLOCK
        ) < 0)
    {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);

        return false;
    }

    const pid_t childPid = ::fork();

    if (childPid < 0)
    {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);

        std::cerr
            << "Failed to create FFmpeg process\n";

        return false;
    }

    if (childPid == 0)
    {
        // Isolate FFmpeg from terminal signals.

        if (::setsid() < 0)
        {
            constexpr char message[] =
                "Failed to isolate FFmpeg process\n";

            ::write(
                STDERR_FILENO,
                message,
                sizeof(message) - 1
            );

            ::_exit(126);
        }

        ::close(pipeFds[1]);

        if (pipeFds[0] != STDIN_FILENO)
        {
            if (::dup2(
                    pipeFds[0],
                    STDIN_FILENO
                ) < 0)
            {
                ::_exit(126);
            }

            ::close(pipeFds[0]);
        }
        else
        {
            if (::fcntl(
                    STDIN_FILENO,
                    F_SETFD,
                    0
                ) < 0)
            {
                ::_exit(126);
            }
        }

        ::signal(SIGINT, SIG_IGN);
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGPIPE, SIG_DFL);

        ::execlp(
            "ffmpeg",
            "ffmpeg",

            "-hide_banner",
            "-loglevel", "warning",
            "-nostdin",

            "-f", "mpegts",
            "-i", "pipe:0",

            "-map", "0:v:0",
            "-map", "0:a:0",

            "-c", "copy",

            "-f", "hls",

            "-hls_time",
            segmentDuration.c_str(),

            "-hls_list_size",
            playlistSize.c_str(),

            "-hls_delete_threshold",
            deleteThreshold.c_str(),

            "-hls_segment_type",
            "mpegts",

            "-hls_flags",
            hlsFlags.c_str(),

            "-hls_segment_filename",
            segmentPath.c_str(),

            "-y",

            playlistPath.c_str(),

            static_cast<char*>(nullptr)
        );

        ::_exit(127);
    }

    ::close(pipeFds[0]);

    inputFd_ = pipeFds[1];
    processId_ = childPid;

    std::cout
        << "HLS segmenter started\n"
        << "FFmpeg PID: "
        << processId_ << '\n'
        << "HLS output: "
        << config.outputDirectory << '\n'
        << "HLS playlist: "
        << config.playlistFilename << '\n'
        << "Automatic segment deletion: "
        << (config.deleteSegments
                ? "ENABLED"
                : "DISABLED")
        << '\n';

    return true;
}

bool HlsSegmenter::write(
    const std::uint8_t* data,
    std::size_t size
)
{
    if (data == nullptr ||
        size == 0 ||
        inputFd_ < 0)
    {
        return false;
    }

    if (!isRunning())
    {
        std::cerr
            << "FFmpeg process is not running\n";

        return false;
    }

    std::size_t totalWritten = 0;

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(500);

    while (totalWritten < size)
    {
        const auto now =
            std::chrono::steady_clock::now();

        if (now >= deadline)
        {
            std::cerr
                << "HLS pipe write timeout\n";

            return false;
        }

        const ssize_t written =
            ::write(
                inputFd_,
                data + totalWritten,
                size - totalWritten
            );

        if (written > 0)
        {
            totalWritten +=
                static_cast<std::size_t>(
                    written
                );

            continue;
        }

        if (written < 0 &&
            errno == EINTR)
        {
            continue;
        }

        if (written < 0 &&
            (errno == EAGAIN ||
             errno == EWOULDBLOCK))
        {
            pollfd descriptor{};

            descriptor.fd = inputFd_;
            descriptor.events = POLLOUT;

            const auto remaining =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(deadline - now);

            const int result =
                ::poll(
                    &descriptor,
                    1,
                    static_cast<int>(
                        remaining.count()
                    )
                );

            if (result < 0 &&
                errno == EINTR)
            {
                continue;
            }

            if (result <= 0 ||
                (descriptor.revents &
                 (POLLERR |
                  POLLHUP |
                  POLLNVAL)))
            {
                std::cerr
                    << "HLS pipe unavailable\n";

                return false;
            }

            continue;
        }

        const int writeError = errno;

        std::cerr
            << "Failed to write MPEG-TS data: "
            << std::error_code(
                writeError,
                std::generic_category()
            ).message()
            << '\n';

        return false;
    }

    return true;
}

void HlsSegmenter::closeInput()
{
    if (inputFd_ >= 0)
    {
        ::close(inputFd_);
        inputFd_ = -1;
    }
}

bool HlsSegmenter::isRunning()
{
    if (processId_ <= 0)
    {
        return false;
    }

    int status = 0;

    pid_t result;

    do
    {
        result = ::waitpid(
            processId_,
            &status,
            WNOHANG
        );
    }
    while (result < 0 &&
           errno == EINTR);

    if (result == 0)
    {
        return true;
    }

    if (result == processId_)
    {
        if (WIFEXITED(status))
        {
            std::cout
                << "FFmpeg exit code: "
                << WEXITSTATUS(status)
                << '\n';
        }
        else if (WIFSIGNALED(status))
        {
            std::cerr
                << "FFmpeg terminated by signal: "
                << WTERMSIG(status)
                << '\n';
        }

        processId_ = -1;

        closeInput();

        return false;
    }

    if (result < 0 && errno == ECHILD)
    {
        processId_ = -1;

        closeInput();
    }

    return false;
}

void HlsSegmenter::stop()
{
    closeInput();

    if (processId_ <= 0)
    {
        return;
    }

    // Wait for normal EOF shutdown.

    for (int i = 0; i < 30; ++i)
    {
        if (!isRunning())
        {
            return;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cerr
        << "FFmpeg shutdown timeout, sending SIGTERM\n";

    ::kill(processId_, SIGTERM);

    for (int i = 0; i < 10; ++i)
    {
        if (!isRunning())
        {
            return;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cerr
        << "FFmpeg termination timeout, sending SIGKILL\n";

    ::kill(processId_, SIGKILL);

    while (::waitpid(
               processId_,
               nullptr,
               0
           ) < 0 &&
           errno == EINTR)
    {
    }

    processId_ = -1;
}

pid_t HlsSegmenter::processId() const
{
    return processId_;
}
