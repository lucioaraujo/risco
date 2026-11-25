// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace risco {

enum class BytebeatOp : std::uint8_t {
    constant, time, negate, bitNot,
    add, subtract, multiply, divide, modulo,
    bitAnd, bitOr, bitXor, shiftLeft, shiftRight,
    sine, cosine, absolute, hyperbolicTangent
};

struct BytebeatInstruction {
    BytebeatOp op = BytebeatOp::constant;
    std::uint32_t value = 0;
    double realValue = 0.0;
};

struct BytebeatProgram {
    std::array<BytebeatInstruction, 64> code{};
    std::uint8_t count = 0;
    std::uint8_t maxStack = 0;
    bool valid = false;
    bool floating = false; // Explicit f: prefix. t in seconds, output -1..1, 8 kHz clock.
};

// Grammar: t, unsigned decimal numbers, parentheses, unary + - ~,
// and binary + - * / % & | ^ << >>. Parsing happens off the audio thread.
bool compileBytebeat(const std::string& expression, BytebeatProgram& output,
                     std::string& error);

// Bounded, allocation-free evaluator. Arithmetic wraps as unsigned 32-bit;
// division or modulo by zero returns zero, and shifts are reduced to 0..31.
std::uint32_t evaluateBytebeat(const BytebeatProgram& program,
                               std::uint32_t time) noexcept;
// Floatbeat has no bitwise operations or mutable state. Same bounded instruction/stack budget.
// Invalid arithmetic returns 0; intermediate values are finite and bounded to +/-1e12.
double evaluateFloatbeat(const BytebeatProgram& program, double seconds) noexcept;

} // namespace risco
