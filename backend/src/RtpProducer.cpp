#include "RtpProducer.hpp"
#include "RtpParser.hpp"
#include "TsParser.hpp"

#include <cerrno>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

RtpProducer::RtpProducer(
    BoundedTsQueue& queue,
    std::uint16_t port
)
    : queue_(queue),
      port_(port),
      patAssembler_(TsParser::PAT_PID)
{
}

bool RtpProducer::initialize()
{
    if (initialized_ || hasFatalError() || isFinished())
    {
        return false;
    }

    if (!socket_.open(port_))
    {
        fail("Failed to open UDP socket");

        return false;
    }

    if (!socket_.setReceiveTimeout(
            RECEIVE_TIMEOUT_MS
        ))
    {
        socket_.close();

        fail("Failed to configure UDP timeout");

        return false;
    }

    if (!socket_.setReceiveBufferSize(
            RECEIVE_BUFFER_SIZE
        ))
    {
        std::cerr
            << "[WARNING] Cannot configure "
            << "UDP receive buffer\n";
    }

    initialized_ = true;

    return true;
}

void RtpProducer::run()
{
    if (!initialized_)
    {
        fail("RTP producer is not initialized");

        finished_.store(true);

        return;
    }

    lastValidSignal_ =
        std::chrono::steady_clock::now();

    auto lastStatistics = lastValidSignal_;

    std::cout
        << "[INFO] RTP producer started\n"
        << "Listening on UDP port "
        << port_ << '\n'
        << "Waiting for RTP signal...\n";

    try
    {
        while (
            !stopRequested_.load() &&
            !hasFatalError()
        )
        {
            if (queue_.statistics().closed)
            {
                break;
            }

            checkSignal();

            const auto now =
                std::chrono::steady_clock::now();

            if (
                now - lastStatistics >=
                STATISTICS_INTERVAL
            )
            {
                publishStatistics();

                lastStatistics = now;
            }

            const ssize_t received =
                socket_.receive(
                    receiveBuffer_.data(),
                    receiveBuffer_.size()
                );

            if (received < 0)
            {
                const int receiveError = errno;

                if (stopRequested_.load())
                {
                    break;
                }

                if (receiveError == EINTR)
                {
                    continue;
                }

                if (
                    receiveError == EAGAIN ||
                    receiveError == EWOULDBLOCK
                )
                {
                    ++localStatistics_.receiveTimeouts;

                    continue;
                }

                ++localStatistics_.receiveErrors;

                fail("UDP receive error");

                break;
            }

            ++localStatistics_.receivedDatagrams;

            if (!processDatagram(
                    receiveBuffer_.data(),
                    static_cast<std::size_t>(received)
                ))
            {
                break;
            }
        }
    }
    catch (const std::exception& error)
    {
        fail(
            "RTP producer exception: " +
            std::string(error.what())
        );
    }
    catch (...)
    {
        fail("Unknown RTP producer exception");
    }

    socket_.close();

    if (hasFatalError())
    {
        queue_.abort();
    }
    else
    {
        queue_.close();
    }

    publishStatistics();

    initialized_ = false;

    finished_.store(true);

    std::cout << "[INFO] RTP producer stopped\n";
}

bool RtpProducer::processDatagram(
    const std::uint8_t* data,
    std::size_t size
)
{
    RtpPacket rtpPacket{};

    if (
        !RtpParser::parse(data, size, rtpPacket) ||
        rtpPacket.payloadType !=
            RtpParser::MPEG_TS_PAYLOAD_TYPE ||
        rtpPacket.payloadSize == 0 ||
        rtpPacket.payloadSize %
            TsParser::PACKET_SIZE != 0
    )
    {
        ++localStatistics_.invalidRtpPackets;

        pendingDiscontinuity_ = true;

        resetAssemblers();

        return true;
    }

    if (!hasActiveSsrc_)
    {
        activeSsrc_ = rtpPacket.ssrc;

        hasActiveSsrc_ = true;

        std::cout
            << "[INFO] Active RTP SSRC: "
            << activeSsrc_ << '\n';
    }

    if (rtpPacket.ssrc != activeSsrc_)
    {
        ++localStatistics_.unexpectedSsrcPackets;

        return true;
    }

    ++localStatistics_.validRtpPackets;

    if (!localStatistics_.signalAvailable)
    {
        std::cout
            << "[INFO] RTP signal available\n";

        localStatistics_.signalAvailable = true;

        signalLossReported_ = false;
    }

    lastValidSignal_ =
        std::chrono::steady_clock::now();

    if (hasPreviousSequence_)
    {
        const auto expected =
            static_cast<std::uint16_t>(
                previousSequence_ + 1
            );

        if (rtpPacket.sequenceNumber != expected)
        {
            ++localStatistics_.rtpDiscontinuities;

            pendingDiscontinuity_ = true;

            resetAssemblers();
        }
    }

    previousSequence_ =
        rtpPacket.sequenceNumber;

    hasPreviousSequence_ = true;

    TsDataBlock block;

    block.rtpSequenceNumber =
        rtpPacket.sequenceNumber;

    block.rtpTimestamp =
        rtpPacket.timestamp;

    block.receivedAt = lastValidSignal_;

    block.data.reserve(
        rtpPacket.payloadSize
    );

    for (
        std::size_t offset = 0;
        offset < rtpPacket.payloadSize;
        offset += TsParser::PACKET_SIZE
    )
    {
        const std::uint8_t* tsData =
            rtpPacket.payload + offset;

        TsPacket tsPacket{};

        if (!TsParser::parse(
                tsData,
                TsParser::PACKET_SIZE,
                tsPacket
            ))
        {
            ++localStatistics_.invalidTsPackets;

            pendingDiscontinuity_ = true;

            resetAssemblers();

            continue;
        }

        ++localStatistics_.validTsPackets;

        if (tsPacket.transportError)
        {
            ++localStatistics_.transportErrors;

            pendingDiscontinuity_ = true;

            if (tsPacket.pid == TsParser::PAT_PID)
            {
                patAssembler_.reset();
            }

            auto assembler =
                pmtAssemblers_.find(tsPacket.pid);

            if (assembler != pmtAssemblers_.end())
            {
                assembler->second.reset();
            }

            continue;
        }

        // Data flow: preserve the complete TS packet.

        block.data.insert(
            block.data.end(),
            tsData,
            tsData + TsParser::PACKET_SIZE
        );

        // Metadata flow: PAT and PMT processing.

        if (tsPacket.pid == TsParser::PAT_PID)
        {
            processPat(tsPacket);
        }
        else
        {
            processPmt(tsPacket);
        }
    }

    if (block.empty())
    {
        return true;
    }

    block.discontinuity = pendingDiscontinuity_;

    const std::size_t bytes = block.size();

    const auto result =
        queue_.tryPush(std::move(block));

    switch (result)
    {
        case BoundedTsQueue::PushResult::Accepted:
        {
            ++localStatistics_.queuedBlocks;

            localStatistics_.queuedBytes += bytes;

            pendingDiscontinuity_ = false;

            return true;
        }

        case BoundedTsQueue::PushResult::Full:
        {
            ++localStatistics_.droppedBlocks;

            localStatistics_.droppedBytes += bytes;

            pendingDiscontinuity_ = true;

            return true;
        }

        case BoundedTsQueue::PushResult::Invalid:
        {
            ++localStatistics_.droppedBlocks;

            localStatistics_.droppedBytes += bytes;

            fail("Invalid TS block rejected by queue");

            return false;
        }

        case BoundedTsQueue::PushResult::Closed:
        {
            ++localStatistics_.droppedBlocks;

            localStatistics_.droppedBytes += bytes;

            return false;
        }
    }

    fail("Unexpected queue push result");

    return false;
}

void RtpProducer::processPat(
    const TsPacket& packet
)
{
    const auto sections =
        patAssembler_.push(packet);

    for (const auto& section : sections)
    {
        PatTable table{};

        if (!PatParser::parse(
                section.data(),
                section.size(),
                table
            ))
        {
            ++localStatistics_.invalidPatSections;

            continue;
        }

        ++localStatistics_.validPatSections;

        const auto key =
            std::make_tuple(
                table.transportStreamId,
                table.versionNumber,
                table.sectionNumber,
                table.currentNextIndicator
            );

        if (printedPatSections_.insert(key).second)
        {
            printPat(table);
        }

        if (!table.currentNextIndicator)
        {
            continue;
        }

        for (const auto& program : table.programs)
        {
            const auto previous =
                programs_.find(
                    program.programNumber
                );

            if (
                previous != programs_.end() &&
                previous->second != program.pmtPid
            )
            {
                programTables_.erase(
                    program.programNumber
                );
            }

            programs_[program.programNumber] =
                program.pmtPid;

            pmtAssemblers_.try_emplace(
                program.pmtPid,
                program.pmtPid
            );
        }
    }
}

void RtpProducer::processPmt(
    const TsPacket& packet
)
{
    auto assembler =
        pmtAssemblers_.find(packet.pid);

    if (assembler == pmtAssemblers_.end())
    {
        return;
    }

    const auto sections =
        assembler->second.push(packet);

    for (const auto& section : sections)
    {
        PmtTable table{};

        if (!PmtParser::parse(
                section.data(),
                section.size(),
                table
            ))
        {
            ++localStatistics_.invalidPmtSections;

            continue;
        }

        const auto program =
            programs_.find(table.programNumber);

        if (
            program == programs_.end() ||
            program->second != packet.pid
        )
        {
            ++localStatistics_.unexpectedPmtSections;

            continue;
        }

        ++localStatistics_.validPmtSections;

        const auto key =
            std::make_tuple(
                table.programNumber,
                packet.pid,
                table.versionNumber,
                table.currentNextIndicator
            );

        if (printedPmtSections_.insert(key).second)
        {
            printPmt(packet.pid, table);
        }

        if (table.currentNextIndicator)
        {
            programTables_[table.programNumber] =
                table;
        }
    }
}

void RtpProducer::resetAssemblers()
{
    patAssembler_.reset();

    for (auto& entry : pmtAssemblers_)
    {
        entry.second.reset();
    }
}

void RtpProducer::checkSignal()
{
    const auto now =
        std::chrono::steady_clock::now();

    if (
        !signalLossReported_ &&
        now - lastValidSignal_ >= SIGNAL_TIMEOUT
    )
    {
        std::cout
            << "[WARNING] RTP signal not available\n";

        localStatistics_.signalAvailable = false;

        signalLossReported_ = true;

        pendingDiscontinuity_ = true;

        resetAssemblers();
    }
}

void RtpProducer::publishStatistics()
{
    localStatistics_.detectedPrograms =
        programs_.size();

    localStatistics_.resolvedPmtTables =
        programTables_.size();

    localStatistics_.programs = programs_;
    localStatistics_.programTables = programTables_;

    std::lock_guard<std::mutex> lock(stateMutex_);

    publishedStatistics_ = localStatistics_;
}

void RtpProducer::requestStop() noexcept
{
    stopRequested_.store(true);
}

void RtpProducer::fail(
    const std::string& message
)
{
    {
        std::lock_guard<std::mutex>
            lock(stateMutex_);

        if (lastError_.empty())
        {
            lastError_ = message;
        }
    }

    fatalError_.store(true);

    queue_.abort();

    std::cerr
        << "[ERROR] RTP producer: "
        << message << '\n';
}

bool RtpProducer::hasFatalError() const noexcept
{
    return fatalError_.load();
}

bool RtpProducer::isFinished() const noexcept
{
    return finished_.load();
}

RtpProducerStatistics RtpProducer::statistics() const
{
    std::lock_guard<std::mutex>
        lock(stateMutex_);

    return publishedStatistics_;
}

std::string RtpProducer::lastError() const
{
    std::lock_guard<std::mutex>
        lock(stateMutex_);

    return lastError_;
}

void RtpProducer::printPat(
    const PatTable& table
)
{
    std::ostringstream output;

    output
        << "\n========== PAT ==========\n"
        << "Transport stream ID: "
        << table.transportStreamId << '\n'
        << "Version: "
        << static_cast<int>(table.versionNumber)
        << '\n'
        << "Current table: "
        << (table.currentNextIndicator ? "YES" : "NO")
        << '\n';

    for (const auto& program : table.programs)
    {
        output
            << "Program "
            << program.programNumber
            << " -> PMT PID 0x"
            << std::hex
            << std::setw(4)
            << std::setfill('0')
            << program.pmtPid
            << std::dec
            << " (" << program.pmtPid << ")\n";
    }

    if (table.hasNetworkPid)
    {
        output
            << "Network PID: "
            << table.networkPid << '\n';
    }

    output
        << "CRC: VALID\n"
        << "=========================\n";

    std::cout << output.str();
}

void RtpProducer::printPmt(
    std::uint16_t pmtPid,
    const PmtTable& table
)
{
    std::ostringstream output;

    output
        << "\n========== PMT ==========\n"
        << "Program: "
        << table.programNumber << '\n'
        << "Version: "
        << static_cast<int>(table.versionNumber)
        << '\n'
        << "Current table: "
        << (table.currentNextIndicator ? "YES" : "NO")
        << '\n'
        << "PMT PID: "
        << pmtPid << '\n'
        << "PCR PID: "
        << table.pcrPid << '\n';

    for (const auto& stream : table.streams)
    {
        output
            << PmtParser::categoryName(
                stream.category
            )
            << " | "
            << PmtParser::streamTypeName(
                stream.streamType
            )
            << " | PID 0x"
            << std::hex
            << std::setw(4)
            << std::setfill('0')
            << stream.elementaryPid
            << std::dec
            << " (" << stream.elementaryPid << ")\n";
    }

    output
        << "CRC: VALID\n"
        << "=========================\n";

    std::cout << output.str();
}
