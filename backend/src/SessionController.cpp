#include "SessionController.hpp"

#include <filesystem>
#include <system_error>
#include <exception>
#include <iostream>
#include <limits>

SessionController::SessionController(
    const ServiceManager& serviceManager,
    const HlsConfig& hlsConfig,
    const BufferConfig& bufferConfig
)
    : serviceManager_(serviceManager),
      hlsConfig_(hlsConfig),
      bufferConfig_(bufferConfig),
      headend_(),
      playback_(2000)
{
}

SessionController::~SessionController()
{
    stop();
}

bool SessionController::start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != SessionState::Idle &&
        state_ != SessionState::Error)
    {
        return false;
    }

    const auto service =
        serviceManager_.firstService();

    if (!service)
    {
        failLocked("No TV services available");

        return false;
    }

    return selectServiceLocked(*service);
}

void SessionController::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == SessionState::Idle &&
        !activeSession_ &&
        headend_.processId() <= 0)
    {
        return;
    }

    state_ = SessionState::Stopping;

    if (!stopSessionLocked())
    {
        return;
    }

    playback_.reset(generationId_);

    lastError_.clear();

    state_ = SessionState::Idle;
}

bool SessionController::selectService(
    std::uint16_t serviceId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto service =
        serviceManager_.findById(serviceId);

    if (!service)
    {
        return false;
    }

    return selectServiceLocked(*service);
}

bool SessionController::selectChannel(
    unsigned int channelNumber
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto service =
        serviceManager_.findByNumber(channelNumber);

    if (!service)
    {
        return false;
    }

    return selectServiceLocked(*service);
}

bool SessionController::nextChannel()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activeSession_)
    {
        return false;
    }

    const auto service =
        serviceManager_.nextService(
            activeSession_->service().serviceId
        );

    if (!service)
    {
        return false;
    }

    return selectServiceLocked(*service);
}

bool SessionController::previousChannel()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activeSession_)
    {
        return false;
    }

    const auto service =
        serviceManager_.previousService(
            activeSession_->service().serviceId
        );

    if (!service)
    {
        return false;
    }

    return selectServiceLocked(*service);
}

bool SessionController::canControlPlaybackLocked(
    std::uint64_t generationId
) const
{
    return
        state_ == SessionState::Running &&
        activeSession_ != nullptr &&
        activeSession_->isRunning() &&
        !activeSession_->hasFatalError() &&
        generationId == generationId_ &&
        generationId ==
            activeSession_->generationId();
}

bool SessionController::pause(
    std::uint64_t generationId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.pause();
}

bool SessionController::resume(
    std::uint64_t generationId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.resume();
}

bool SessionController::seek(
    std::uint64_t generationId,
    std::int64_t positionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.seek(positionMs);
}

bool SessionController::goLive(
    std::uint64_t generationId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.goLive();
}

bool SessionController::goOldest(
    std::uint64_t generationId
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.goOldest();
}

bool SessionController::acknowledgeSeek(
    std::uint64_t generationId,
    std::uint64_t seekRevision,
    std::uint64_t actualPositionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.acknowledgeSeek(
        generationId,
        seekRevision,
        actualPositionMs
    );
}

bool SessionController::reportPlaybackPosition(
    std::uint64_t generationId,
    std::uint64_t seekRevision,
    std::uint64_t positionMs
)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!canControlPlaybackLocked(generationId))
    {
        return false;
    }

    return playback_.reportPlaybackPosition(
        generationId,
        seekRevision,
        positionMs
    );
}

std::vector<TvService>
SessionController::services() const
{
    return serviceManager_.services();
}

bool SessionController::selectServiceLocked(
    const TvService& service
)
{
    if (activeSession_ &&
        state_ == SessionState::Running &&
        activeSession_->service().serviceId ==
            service.serviceId &&
        activeSession_->isRunning() &&
        headend_.isRunning())
    {
        return true;
    }

    if (activeSession_ ||
        headend_.processId() > 0)
    {
        state_ = SessionState::Switching;

        if (!stopSessionLocked())
        {
            return false;
        }

	std::error_code ec;

	std::filesystem::remove_all(
	    hlsConfig_.outputDirectory /
	        std::to_string(generationId_),
	    ec
	);

	if (ec)
	{
	    failLocked(
	        "Failed to remove previous HLS buffer: " +
	        ec.message()
	    );

	    return false;
	}

    }

    return startSessionLocked(service);
}

bool SessionController::startSessionLocked(
    const TvService& service
)
{
    if (generationId_ ==
        std::numeric_limits<std::uint64_t>::max())
    {
        failLocked("Session generation overflow");

        return false;
    }

    state_ = SessionState::Starting;

    lastError_.clear();

    ++generationId_;

    playback_.reset(generationId_);

    try
    {
	HlsConfig sessionHlsConfig = hlsConfig_;
	BufferConfig sessionBufferConfig = bufferConfig_;

	const auto sessionDirectory =
	    hlsConfig_.outputDirectory /
	    std::to_string(generationId_);

	sessionHlsConfig.outputDirectory =
	    sessionDirectory;

	sessionBufferConfig.storageDirectory =
	    sessionDirectory;

	activeSession_ =
	    std::make_unique<ActiveSession>(
	        service,
	        generationId_,
	        sessionHlsConfig,
	        sessionBufferConfig
	    );

     if (!activeSession_->start())
        {
            failLocked(
                "Failed to start session: " +
                activeSession_->lastError()
            );

            return false;
        }

        if (!headend_.start(service))
        {
            failLocked(
                "Failed to start Headend: " +
                headend_.lastError()
            );

            return false;
        }

        if (!playback_.start())
        {
            failLocked(
                "Failed to start Playback Manager"
            );

            return false;
        }

        state_ = SessionState::Running;

        std::cout
            << "[INFO] Active service: "
            << service.name
            << ", channel: "
            << service.channelNumber
            << ", generation: "
            << generationId_
            << '\n';

        return true;
    }
    catch (const std::exception& error)
    {
        failLocked(
            std::string("Session creation failed: ") +
            error.what()
        );
    }
    catch (...)
    {
        failLocked(
            "Unknown session creation error"
        );
    }

    return false;
}

bool SessionController::stopSessionLocked()
{
    std::string shutdownError;

    if (!headend_.stop())
    {
        shutdownError =
            "Headend shutdown failed: " +
            headend_.lastError();
    }

    if (activeSession_)
    {
        activeSession_->stop();

        if (activeSession_->hasFatalError())
        {
            if (!shutdownError.empty())
            {
                shutdownError += "; ";
            }

            shutdownError +=
                "Session shutdown failed: " +
                activeSession_->lastError();
        }

        activeSession_.reset();
    }

    if (!shutdownError.empty())
    {
        failLocked(shutdownError);

        return false;
    }

    return true;
}

bool SessionController::update()
{
    std::lock_guard<std::mutex> lock(mutex_);

    return updateLocked();
}

bool SessionController::updateLocked()
{
    if (state_ == SessionState::Error)
    {
        return false;
    }

    if (state_ == SessionState::Idle)
    {
        return true;
    }

    if (!activeSession_)
    {
        failLocked("Active session is missing");

        return false;
    }

    if (!headend_.isRunning())
    {
        failLocked(
            "Headend stopped unexpectedly: " +
            headend_.lastError()
        );

        return false;
    }

    if (activeSession_->hasFatalError())
    {
        failLocked(activeSession_->lastError());

        return false;
    }

    if (!activeSession_->isRunning())
    {
        failLocked(
            "Active session stopped unexpectedly"
        );

        return false;
    }

    const BufferInterval interval =
        activeSession_->publishedInterval();

    if (!playback_.updateBufferInterval(
            activeSession_->generationId(),
            interval
        ))
    {
        failLocked(
            "Invalid published buffer interval"
        );

        return false;
    }

    return true;
}

SessionSnapshot SessionController::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    SessionSnapshot result;

    result.state = state_;

    result.generationId = generationId_;

    result.playback = playback_.snapshot();

    result.errorMessage = lastError_;

    if (activeSession_)
    {
        result.activeService =
            activeSession_->service();
    }

    return result;
}

std::optional<ActiveSessionStatistics>
SessionController::statistics() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activeSession_)
    {
        return std::nullopt;
    }

    return activeSession_->statistics();
}

void SessionController::failLocked(
    const std::string& message
)
{
    lastError_ =
        message.empty()
            ? "Unknown session error"
            : message;

    state_ = SessionState::Error;

    if (!headend_.stop())
    {
        lastError_ +=
            "; Headend cleanup failed: " +
            headend_.lastError();
    }

    if (activeSession_)
    {
        activeSession_->stop();

        activeSession_.reset();
    }

    if (generationId_ > 0)
    {
        std::error_code ec;

        std::filesystem::remove_all(
            hlsConfig_.outputDirectory /
                std::to_string(generationId_),
            ec
        );

        if (ec)
        {
            lastError_ +=
                "; HLS buffer cleanup failed: " +
                ec.message();
        }
    }

    playback_.reset(generationId_);

    playback_.setError(lastError_);

    std::cerr
        << "[ERROR] SessionController: "
        << lastError_
        << '\n';
}
