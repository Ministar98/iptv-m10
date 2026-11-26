#pragma once

#include "TvService.hpp"
#include "BoundedTsQueue.hpp"
#include "RtpProducer.hpp"
#include "HlsConsumer.hpp"

#include <cstdint>
#include <string>
#include <thread>

struct ActiveSessionStatistics
{
    RtpProducerStatistics producer{};

    BoundedTsQueue::Statistics queue{};

    HlsConsumerStatistics consumer{};
};

class ActiveSession
{
public:
    ActiveSession(
        const TvService& service,
        std::uint64_t generationId,
        const HlsConfig& hlsConfig,
        const BufferConfig& bufferConfig
    );

    ~ActiveSession();

    ActiveSession(const ActiveSession&) = delete;

    ActiveSession& operator=(
        const ActiveSession&
    ) = delete;

    bool start();

    void stop();

    bool isRunning() const noexcept;

    bool hasFatalError() const noexcept;

    std::string lastError() const;

    BufferInterval publishedInterval() const;

    ActiveSessionStatistics statistics() const;

    const TvService& service() const noexcept;

    std::uint64_t generationId() const noexcept;

private:
    const TvService service_;

    const std::uint64_t generationId_;

    BoundedTsQueue queue_;

    RtpProducer producer_;

    HlsConsumer consumer_;

    std::thread producerThread_;

    std::thread consumerThread_;

    bool startAttempted_ = false;

    bool running_ = false;

    bool stopped_ = false;

    bool lifecycleError_ = false;

    std::string lastError_;

    void fail(const std::string& message);
};
