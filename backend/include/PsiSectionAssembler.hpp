#pragma once

#include "TsParser.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using PsiSection = std::vector<std::uint8_t>;

class PsiSectionAssembler
{
public:
    static constexpr std::size_t SECTION_HEADER_SIZE = 3;

    static constexpr std::size_t MAX_SECTION_LENGTH = 1021;

    explicit PsiSectionAssembler(std::uint16_t pid);

    std::vector<PsiSection> push(const TsPacket& packet);

    void reset();

    std::uint16_t pid() const;

private:
    std::uint16_t pid_;

    PsiSection buffer_;

    std::size_t expectedSize_ = 0;

    bool hasPreviousCounter_ = false;

    std::uint8_t previousCounter_ = 0;

    void clearSection();

    bool appendByte(
        std::uint8_t value,
        std::vector<PsiSection>& completed
    );

    void consume(
        const std::uint8_t* data,
        std::size_t size,
        bool allowNewSections,
        std::vector<PsiSection>& completed
    );
};
