#include "ActiveSession.hpp"

#include <exception>
#include <iostream>

ActiveSession::ActiveSession(
    const TvService& service,
    std::uint64_t generationId,
    const HlsConfig& hlsConfig,
    const BufferConfig& bufferConfig
)
    : service_(service),
      generationId_(generationId),
      queue_(),
      producer_(queue_, service.rtpPort),
      consumer_(queue_, hlsConfig, bufferConfig)
{
}

ActiveSession::~ActiveSession()
{
    stop();
}

bool ActiveSession::start()
{
    if (startAttempted_ || stopped_)
    {
        return false;
    }

    startAttempted_ = true;

    std::cout
        << "[INFO] Starting session "
        << generationId_
        << ", service "
        << service_.serviceId
        << '\n';

    try
    {
        if (!producer_.initialize())
        {
            fail(
                "RTP Producer initialization failed: " +
                producer_.lastError()
            );

            stop();

            return false;
        }

        if (!consumer_.initialize())
        {
            fail(
                "HLS Consumer initialization failed: " +
                consumer_.lastError()
            );

            stop();

            return false;
        }

        consumerThread_ = std::thread(
            &HlsConsumer::run,
            &consumer_
        );

        producerThread_ = std::thread(
            &RtpProducer::run,
            &producer_
        );

        running_ = true;

        std::cout
            << "[INFO] Session started: "
            << generationId_
            << '\n';

        return true;
    }
    catch (const std::exception& error)
    {
        fail(
            std::string("Session startup failed: ") +
            error.what()
        );
    }
    catch (...)
    {
        fail("Unknown session startup error");
    }

    stop();

    return false;
}

void ActiveSession::stop()
{
    if (stopped_)
    {
        return;
    }

    stopped_ = true;

    if (!startAttempted_)
    {
        return;
    }

    std::cout
        << "[INFO] Stopping session "
        << generationId_
        << '\n';

    producer_.requestStop();

    if (hasFatalError())
    {
        queue_.abort();
    }

    if (producerThread_.joinable())
    {
        producerThread_.join();
    }

    if (hasFatalError())
    {
        queue_.abort();
    }
    else
    {
        queue_.close();
    }

    if (consumerThread_.joinable())
    {
        consumerThread_.join();
    }

    running_ = false;

    if (hasFatalError())
    {
        std::cerr
            << "[ERROR] Session stopped with error: "
            << lastError()
            << '\n';
    }
    else
    {
        std::cout
            << "[INFO] Session stopped: "
            << generationId_
            << '\n';
    }
}

bool ActiveSession::isRunning() const noexcept
{
    return running_ &&
           !stopped_ &&
           !producer_.isFinished() &&
           !consumer_.isFinished() &&
           !hasFatalError();
}

bool ActiveSession::hasFatalError() const noexcept
{
    return lifecycleError_ ||
           producer_.hasFatalError() ||
           consumer_.hasFatalError();
}

std::string ActiveSession::lastError() const
{
    if (lifecycleError_)
    {
        return lastError_;
    }

    if (producer_.hasFatalError())
    {
        return producer_.lastError();
    }

    if (consumer_.hasFatalError())
    {
        return consumer_.lastError();
    }

    return {};
}

BufferInterval
ActiveSession::publishedInterval() const
{
    return consumer_.publishedInterval();
}

ActiveSessionStatistics
ActiveSession::statistics() const
{
    ActiveSessionStatistics result;

    result.producer = producer_.statistics();

    result.queue = queue_.statistics();

    result.consumer = consumer_.statistics();

    return result;
}

const TvService&
ActiveSession::service() const noexcept
{
    return service_;
}

std::uint64_t
ActiveSession::generationId() const noexcept
{
    return generationId_;
}

void ActiveSession::fail(
    const std::string& message
)
{
    lifecycleError_ = true;

    lastError_ = message;

    std::cerr
        << "[ERROR] ActiveSession: "
        << message
        << '\n';
}
