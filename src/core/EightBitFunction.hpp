// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once
#include <cstdint>

namespace risco {
// Função autoral simples de fase: a rampa e sua versão lenta se combinam.
// O módulo 256 define explicitamente a resolução de amplitude da voz.
inline std::uint8_t eightBitFunction(std::uint32_t phase) noexcept {
    return static_cast<std::uint8_t>((phase + (phase >> 4)) & 0xffu);
}
}
