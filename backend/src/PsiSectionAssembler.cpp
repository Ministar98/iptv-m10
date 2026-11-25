#include "PsiSectionAssembler.hpp"

#include <utility>

PsiSectionAssembler::PsiSectionAssembler(
    std::uint16_t pid
)
    : pid_(pid)
{
}

std::uint16_t PsiSectionAssembler::pid() const
{
    return pid_;
}

void PsiSectionAssembler::clearSection()
{
    buffer_.clear();

    expectedSize_ = 0;
}

void PsiSectionAssembler::reset()
{
    clearSection();

    hasPreviousCounter_ = false;

    previousCounter_ = 0;
}

bool PsiSectionAssembler::appendByte(
    std::uint8_t value,
    std::vector<PsiSection>& completed
)
{
    buffer_.push_back(value);

    // Determine section size after receiving
    // the first three bytes.

    if (buffer_.size() == SECTION_HEADER_SIZE)
    {
        const std::size_t sectionLength =
            (static_cast<std::size_t>(
                buffer_[1] & 0x0F
            ) << 8) |
            buffer_[2];

        if (sectionLength > MAX_SECTION_LENGTH)
        {
            clearSection();

            return false;
        }

        expectedSize_ =
            SECTION_HEADER_SIZE + sectionLength;
    }

    // Complete PSI section received.

    if (expectedSize_ != 0 &&
        buffer_.size() == expectedSize_)
    {
        completed.push_back(std::move(buffer_));

        clearSection();
    }

    return true;
}

void PsiSectionAssembler::consume(
    const std::uint8_t* data,
    std::size_t size,
    bool allowNewSections,
    std::vector<PsiSection>& completed
)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (buffer_.empty())
        {
            if (!allowNewSections)
            {
                return;
            }

            // PSI stuffing bytes.

            if (data[i] == 0xFF)
            {
                return;
            }
        }

        if (!appendByte(data[i], completed))
        {
            return;
        }
    }
}

std::vector<PsiSection> PsiSectionAssembler::push(
    const TsPacket& packet
)
{
    std::vector<PsiSection> completed;

    if (packet.pid != pid_)
    {
        return completed;
    }

    if (packet.transportError ||
        packet.scramblingControl != 0)
    {
        reset();

        return completed;
    }

    if (packet.discontinuityIndicator)
    {
        reset();
    }

    if (!packet.hasPayload ||
        packet.payload == nullptr ||
        packet.payloadSize == 0)
    {
        return completed;
    }

    // Continuity counter monitoring.

    if (hasPreviousCounter_)
    {
        if (packet.continuityCounter ==
            previousCounter_)
        {
            // Treat repeated payload counter
            // as a duplicate packet.

            return completed;
        }

        const std::uint8_t expectedCounter =
            static_cast<std::uint8_t>(
                (previousCounter_ + 1) & 0x0F
            );

        if (packet.continuityCounter !=
            expectedCounter)
        {
            clearSection();
        }
    }

    previousCounter_ =
        packet.continuityCounter;

    hasPreviousCounter_ = true;

    if (packet.payloadUnitStart)
    {
        const std::size_t pointerField =
            packet.payload[0];

        const std::size_t availableBytes =
            packet.payloadSize - 1;

        if (pointerField > availableBytes)
        {
            clearSection();

            return completed;
        }

        // Bytes preceding the new section may
        // complete the previous section.

        if (!buffer_.empty())
        {
            consume(
                packet.payload + 1,
                pointerField,
                false,
                completed
            );
        }

        // The pointer identifies the beginning
        // of a new PSI section.

        clearSection();

        const std::size_t offset =
            1 + pointerField;

        consume(
            packet.payload + offset,
            packet.payloadSize - offset,
            true,
            completed
        );
    }
    else
    {
        // Continuation of a previously
        // started PSI section.

        if (!buffer_.empty())
        {
            consume(
                packet.payload,
                packet.payloadSize,
                false,
                completed
            );
        }
    }

    return completed;
}
