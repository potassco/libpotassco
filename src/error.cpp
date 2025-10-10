//
// Copyright (c) 2024 - present, Benjamin Kaufmann
//
// This file is part of Potassco.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
#include <potassco/error.h>

#include <potassco/basic_types.h>
#include <potassco/format.h>

namespace Potassco {
using namespace std::literals;
using ErrorBuffer = BasicCharBufferT<1024>;

constinit AbortHandler g_abort_handler = nullptr;

static ErrorBuffer& appendInfo(ErrorBuffer& buffer, std::string_view type, const ExpressionInfo& expressionInfo,
                               bool addFile) {
    buffer.append(addFile ? ExpressionInfo::relativeFileName(expressionInfo.location)
                          : expressionInfo.location.function_name());
    buffer.append(":"sv).append(expressionInfo.location.line());
    if (addFile) {
        buffer.append(": "sv).append(expressionInfo.location.function_name());
    }
    buffer.append(": "sv);
    auto startExp = expressionInfo.expression.empty() ? ""sv : "'"sv;
    auto endExp   = expressionInfo.expression.empty() ? ""sv : "' "sv;
    return buffer.append(type).append(startExp).append(expressionInfo.expression).append(endExp).append("failed."sv);
}

extern void failAbort(const ExpressionInfo& expressionInfo, const char* fmt, ...) {
    ErrorBuffer buffer;
    auto        hasMessage = fmt && *fmt;
    appendInfo(buffer, "Assertion "sv, expressionInfo, true);
    if (hasMessage) {
        buffer.append("\nmessage: "sv);
        va_list args;
        va_start(args, fmt);
        buffer.vAppendF(fmt, args);
        va_end(args);
    }
    if (g_abort_handler) {
        g_abort_handler(buffer.c_str());
    }
    fprintf(stderr, "%s\n", buffer.c_str());
    std::abort();
}

extern void failThrow(Errc ec, const ExpressionInfo& expressionInfo, const char* fmt, ...) {
    if (ec == Errc::bad_alloc) {
        throw std::bad_alloc();
    }
    ErrorBuffer buffer;
    auto        hasMessage = fmt && *fmt;
    va_list     args;
    va_start(args, fmt);
    if (ec == Errc::precondition_fail) {
        appendInfo(buffer, "Precondition "sv, expressionInfo, false);
        if (hasMessage) {
            buffer.append("\nmessage: ").vAppendF(fmt, args);
        }
        ec = Errc::invalid_argument;
    }
    else {
        if (hasMessage) {
            buffer.vAppendF(fmt, args).append(": "sv);
        }
        auto check = expressionInfo.expression.empty() ? ""sv : "check "sv;
        buffer.append(std::generic_category().message(static_cast<int>(ec))).append("\n"sv);
        appendInfo(buffer, check, expressionInfo, false);
    }
    va_end(args);
    const char* message = buffer.c_str();
    switch (ec) {
        // logic
        case Errc::length_error    : throw std::length_error(message);
        case Errc::invalid_argument: throw std::invalid_argument(message);
        case Errc::domain_error    : throw std::domain_error(message);
        case Errc::out_of_range    : throw std::out_of_range(message);
        // runtime
        case Errc::overflow_error: throw std::overflow_error(message);
        default                  : throw RuntimeError(ec, expressionInfo.location, message);
    }
}

extern AbortHandler setAbortHandler(AbortHandler handler) { return std::exchange(g_abort_handler, handler); }

std::string_view RuntimeError::message() const noexcept {
    auto ret = std::string_view(what());
    return ret.substr(0, ret.find('\n'));
}

std::string_view RuntimeError::details() const noexcept {
    auto ret = std::string_view(what());
    auto pos = ret.find('\n');
    return ret.substr(pos < ret.size() ? pos + 1 : ret.size());
}

} // namespace Potassco
