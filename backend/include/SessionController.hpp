#pragma once

#include "ActiveSession.hpp"
#include "ServiceManager.hpp"
#include "SessionSnapshot.hpp"
#include "PlaybackManager.hpp"
#include "HeadendController.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

class SessionController
{
public:
    SessionController(
        const ServiceManager& serviceManager,
        const HlsConfig& hlsConfig,
        const BufferConfig& bufferConfig
    );

    ~SessionController();

    SessionController(const SessionController&) = delete;

    SessionController& operator=(
        const SessionController&
    ) = delete;

    bool start();

    void stop();

    bool selectService(std::uint16_t serviceId);

    bool selectChannel(unsigned int channelNumber);

    bool nextChannel();

    bool previousChannel();

    bool update();

    SessionSnapshot snapshot() const;

    std::optional<ActiveSessionStatistics>
    statistics() const;

private:
    const ServiceManager serviceManager_;

    const HlsConfig hlsConfig_;

    const BufferConfig bufferConfig_;

    mutable std::mutex mutex_;

    std::unique_ptr<ActiveSession> activeSession_;

    HeadendController headend_;

    PlaybackManager playback_;

    SessionState state_ = SessionState::Idle;

    std::uint64_t generationId_ = 0;

    std::string lastError_;

    bool selectServiceLocked(const TvService& service);

    bool startSessionLocked(const TvService& service);

    bool stopSessionLocked();

    bool updateLocked();

    void failLocked(const std::string& message);
};
