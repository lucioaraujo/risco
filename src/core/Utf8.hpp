// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace risco {

inline bool nextUtf8(const std::string& text, std::size_t& offset,
                     std::uint32_t& codepoint) noexcept {
    if (offset >= text.size()) return false;
    const auto first = static_cast<unsigned char>(text[offset++]);
    if (first < 0x80u) { codepoint = first; return true; }
    int count = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2u && first <= 0xdfu) { count = 1; value = first & 0x1fu; minimum = 0x80u; }
    else if (first >= 0xe0u && first <= 0xefu) { count = 2; value = first & 0x0fu; minimum = 0x800u; }
    else if (first >= 0xf0u && first <= 0xf4u) { count = 3; value = first & 0x07u; minimum = 0x10000u; }
    else return false;
    if (offset + static_cast<std::size_t>(count) > text.size()) return false;
    for (int i = 0; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset++]);
        if ((byte & 0xc0u) != 0x80u) return false;
        value = (value << 6u) | (byte & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) return false;
    codepoint = value;
    return true;
}

inline bool validUtf8(const std::string& text) noexcept {
    std::size_t offset = 0;
    std::uint32_t codepoint = 0;
    while (offset < text.size())
        if (!nextUtf8(text, offset, codepoint)) return false;
    return true;
}

}
