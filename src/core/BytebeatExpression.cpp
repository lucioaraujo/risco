// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo
#include "BytebeatExpression.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>

namespace risco {
namespace {

class Compiler {
public:
    Compiler(const std::string& input, BytebeatProgram& program)
        : source(input), result(program) {
        if (source.rfind("f:", 0) == 0) { result.floating = true; position = 2; }
    }

    bool run(std::string& error) {
        if (source.empty() || source.size() > 128) return fail(error, "Use uma expressão de 1 a 128 caracteres.");
        skipSpace();
        if (!parseExpression(1, 0)) return fail(error, "Expressão inválida perto da posição " + std::to_string(position + 1) + ".");
        skipSpace();
        if (position != source.size()) return fail(error, "Caractere inesperado na posição " + std::to_string(position + 1) + ".");
        if (!validateStack()) return fail(error, "A expressão excede o limite de pilha.");
        result.valid = true;
        error.clear();
        return true;
    }

private:
    const std::string& source;
    BytebeatProgram& result;
    std::size_t position = 0;
    int nesting = 0;

    bool fail(std::string& error, std::string message) {
        result = {};
        error = std::move(message);
        return false;
    }

    void skipSpace() {
        while (position < source.size() &&
               (source[position] == ' ' || source[position] == '\t' ||
                source[position] == '\n' || source[position] == '\r')) ++position;
    }

    bool emit(BytebeatOp op, std::uint32_t value = 0, double realValue = 0) {
        if (result.count >= result.code.size()) return false;
        result.code[result.count++] = {op, value, realValue};
        return true;
    }

    static int precedence(char op, char second = '\0') {
        if (op == '|') return 1;
        if (op == '^') return 2;
        if (op == '&') return 3;
        if ((op == '<' || op == '>') && second == op) return 4;
        if (op == '+' || op == '-') return 5;
        if (op == '*' || op == '/' || op == '%') return 6;
        return 0;
    }

    bool parseExpression(int minimum, int depth) {
        // Parser frames also count precedence/unary calls, not just parentheses.
        // Keep 16 parenthesis levels but allow their bounded internal frames.
        if (depth > 64 || !parseUnary(depth + 1)) return false;
        for (;;) {
            skipSpace();
            if (position >= source.size()) return true;
            const char first = source[position];
            const char second = position + 1 < source.size() ? source[position + 1] : '\0';
            const int level = precedence(first, second);
            if (level < minimum) return true;
            if (result.floating && level <= 4) return false;
            ++position;
            if ((first == '<' || first == '>') && second == first) ++position;
            if (!parseExpression(level + 1, depth + 1)) return false;
            BytebeatOp op;
            switch (first) {
                case '+': op = BytebeatOp::add; break;
                case '-': op = BytebeatOp::subtract; break;
                case '*': op = BytebeatOp::multiply; break;
                case '/': op = BytebeatOp::divide; break;
                case '%': op = BytebeatOp::modulo; break;
                case '&': op = BytebeatOp::bitAnd; break;
                case '|': op = BytebeatOp::bitOr; break;
                case '^': op = BytebeatOp::bitXor; break;
                case '<': op = BytebeatOp::shiftLeft; break;
                case '>': op = BytebeatOp::shiftRight; break;
                default: return false;
            }
            if (!emit(op)) return false;
        }
    }

    bool parseUnary(int depth) {
        if (depth > 64) return false;
        skipSpace();
        if (position >= source.size()) return false;
        const char token = source[position];
        if (token == '+') { ++position; return parseUnary(depth + 1); }
        if (token == '-' || token == '~') {
            ++position;
            if (!parseUnary(depth + 1)) return false;
            if (result.floating && token == '~') return false;
            return emit(token == '-' ? BytebeatOp::negate : BytebeatOp::bitNot);
        }
        if (token == '(') {
            ++position;
            if (++nesting > 16 || !parseExpression(1, depth + 1)) return false;
            skipSpace();
            if (position >= source.size() || source[position] != ')') return false;
            ++position;
            --nesting;
            return true;
        }
        if (result.floating) {
            const std::pair<const char*, BytebeatOp> functions[] = {
                {"sin", BytebeatOp::sine}, {"cos", BytebeatOp::cosine},
                {"abs", BytebeatOp::absolute}, {"tanh", BytebeatOp::hyperbolicTangent}};
            for (const auto& function : functions) {
                const std::string name(function.first);
                if (source.compare(position, name.size(), name) != 0) continue;
                position += name.size(); skipSpace();
                if (position >= source.size() || source[position++] != '(' || !parseExpression(1, depth + 1)) return false;
                skipSpace();
                if (position >= source.size() || source[position++] != ')') return false;
                return emit(function.second);
            }
            if ((token >= '0' && token <= '9') || token == '.') {
                double value = 0, factor = 0.1;
                bool digit = false;
                while (position < source.size() && source[position] >= '0' && source[position] <= '9') {
                    value = value * 10 + source[position++] - '0'; digit = true;
                }
                if (position < source.size() && source[position] == '.') {
                    ++position;
                    while (position < source.size() && source[position] >= '0' && source[position] <= '9') {
                        value += (source[position++] - '0') * factor; factor *= 0.1; digit = true;
                    }
                }
                if (!digit || !std::isfinite(value) || value > 1e12) return false;
                return emit(BytebeatOp::constant, 0, value);
            }
        }
        if (token == 't' || token == 'T') {
            ++position;
            if (position < source.size() && ((source[position] >= 'a' && source[position] <= 'z') ||
                                             (source[position] >= 'A' && source[position] <= 'Z') ||
                                             (source[position] >= '0' && source[position] <= '9') ||
                                             source[position] == '_')) return false;
            return emit(BytebeatOp::time);
        }
        if (token < '0' || token > '9') return false;
        std::uint32_t value = 0;
        while (position < source.size() && source[position] >= '0' && source[position] <= '9') {
            const auto digit = static_cast<std::uint32_t>(source[position++] - '0');
            if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10u) return false;
            value = value * 10u + digit;
        }
        return emit(BytebeatOp::constant, value);
    }

    bool validateStack() {
        int depth = 0;
        int maximum = 0;
        for (std::size_t i = 0; i < result.count; ++i) {
            const auto op = result.code[i].op;
            if (op == BytebeatOp::constant || op == BytebeatOp::time) {
                maximum = std::max(maximum, ++depth);
            } else if (op == BytebeatOp::negate || op == BytebeatOp::bitNot ||
                       op == BytebeatOp::sine || op == BytebeatOp::cosine ||
                       op == BytebeatOp::absolute || op == BytebeatOp::hyperbolicTangent) {
                if (depth < 1) return false;
            } else {
                if (depth < 2) return false;
                --depth;
            }
            if (maximum > 32) return false;
        }
        result.maxStack = static_cast<std::uint8_t>(maximum);
        return depth == 1;
    }
};

} // namespace

bool compileBytebeat(const std::string& expression, BytebeatProgram& output,
                     std::string& error) {
    output = {};
    return Compiler(expression, output).run(error);
}

std::uint32_t evaluateBytebeat(const BytebeatProgram& program,
                               std::uint32_t time) noexcept {
    if (!program.valid) return 0;
    if (program.floating)
        return static_cast<std::uint32_t>(std::lround((evaluateFloatbeat(program, double(time) / 8000.0) + 1.0) * 127.5));
    if (!program.valid || program.count > program.code.size() || program.maxStack == 0 || program.maxStack > 32) return 0;
    std::array<std::uint32_t, 32> stack{};
    std::size_t size = 0;
    for (std::size_t i = 0; i < program.count; ++i) {
        const auto instruction = program.code[i];
        if (instruction.op == BytebeatOp::constant || instruction.op == BytebeatOp::time) {
            if (size >= stack.size()) return 0;
            stack[size++] = instruction.op == BytebeatOp::time ? time : instruction.value;
            continue;
        }
        if (instruction.op == BytebeatOp::negate || instruction.op == BytebeatOp::bitNot) {
            if (size < 1) return 0;
            stack[size - 1] = instruction.op == BytebeatOp::negate
                ? 0u - stack[size - 1] : ~stack[size - 1];
            continue;
        }
        if (size < 2) return 0;
        const auto right = stack[--size];
        auto& left = stack[size - 1];
        switch (instruction.op) {
            case BytebeatOp::add: left += right; break;
            case BytebeatOp::subtract: left -= right; break;
            case BytebeatOp::multiply: left *= right; break;
            case BytebeatOp::divide: left = right == 0 ? 0 : left / right; break;
            case BytebeatOp::modulo: left = right == 0 ? 0 : left % right; break;
            case BytebeatOp::bitAnd: left &= right; break;
            case BytebeatOp::bitOr: left |= right; break;
            case BytebeatOp::bitXor: left ^= right; break;
            case BytebeatOp::shiftLeft: left <<= (right & 31u); break;
            case BytebeatOp::shiftRight: left >>= (right & 31u); break;
            default: return 0;
        }
    }
    return size == 1 ? stack[0] : 0;
}

double evaluateFloatbeat(const BytebeatProgram& program, double seconds) noexcept {
    if (!program.valid || !program.floating || !std::isfinite(seconds) ||
        program.count > program.code.size() || program.maxStack == 0 || program.maxStack > 32) return 0;
    std::array<double, 32> stack{};
    std::size_t size = 0;
    for (std::size_t i = 0; i < program.count; ++i) {
        const auto& instruction = program.code[i];
        if (instruction.op == BytebeatOp::constant || instruction.op == BytebeatOp::time) {
            if (size >= stack.size()) return 0;
            stack[size++] = instruction.op == BytebeatOp::time ? std::clamp(seconds, -1e12, 1e12) : instruction.realValue;
            continue;
        }
        if (instruction.op == BytebeatOp::negate || instruction.op == BytebeatOp::sine ||
            instruction.op == BytebeatOp::cosine || instruction.op == BytebeatOp::absolute || instruction.op == BytebeatOp::hyperbolicTangent) {
            if (size == 0) return 0;
            auto& value = stack[size - 1];
            if (instruction.op == BytebeatOp::negate) value = -value;
            else if (instruction.op == BytebeatOp::sine) value = std::sin(value);
            else if (instruction.op == BytebeatOp::cosine) value = std::cos(value);
            else if (instruction.op == BytebeatOp::absolute) value = std::abs(value);
            else value = std::tanh(value);
            continue;
        }
        if (size < 2) return 0;
        const double right = stack[--size];
        auto& left = stack[size - 1];
        switch (instruction.op) {
            case BytebeatOp::add: left += right; break;
            case BytebeatOp::subtract: left -= right; break;
            case BytebeatOp::multiply: left *= right; break;
            case BytebeatOp::divide: left = right == 0 ? 0 : left / right; break;
            case BytebeatOp::modulo: left = right == 0 ? 0 : std::fmod(left, right); break;
            default: return 0;
        }
        left = std::isfinite(left) ? std::clamp(left, -1e12, 1e12) : 0.0;
    }
    return size == 1 && std::isfinite(stack[0]) ? std::clamp(stack[0], -1.0, 1.0) : 0.0;
}

} // namespace risco
