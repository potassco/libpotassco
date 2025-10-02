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

#if __has_include(<fpu_control.h>)
#include <fpu_control.h>
#endif

#if __has_include(<io.h>)
#include <io.h>
#endif

#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
#if defined(_WIN32) && __has_include(<Windows.h>)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
static void flockfile(FILE* file) { _lock_file(file); }
static void funlockfile(FILE* file) { _unlock_file(file); }
static bool isTerminal(FILE* file) { return _isatty(_fileno(file)); }
static bool isCygPty(FILE* file) {
#if defined(WINDOWS_LEAN_AND_MEAN)
    auto h = (HANDLE) _get_osfhandle(_fileno(file));
    auto t = GetFileType(h);
    if (t == FILE_TYPE_PIPE) {
        union Info {
            FILE_NAME_INFO x;
            WCHAR          b[MAX_PATH + 1 + sizeof(FILE_NAME_INFO)];
        } info;
        if (not GetFileInformationByHandleEx(h, FileNameInfo, &info.x, sizeof(Info))) {
            return false;
        }
        info.x.FileName[info.x.FileNameLength] = 0;
        auto name                              = info.x.FileName;
        return (wcsstr(name, L"\\msys-") == name || wcsstr(name, L"\\cygwin-") == name) &&
               wcsstr(name, L"-pty") != nullptr;
    }
    return t == FILE_TYPE_CHAR;
#else
    return false;
#endif
}
#else
static bool isTerminal(FILE* file) { return isatty(fileno(file)); }
static bool isCygPty(FILE*) { return false; }
#endif

#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <span>
#include <utility>

static auto enableTerminalColors([[maybe_unused]] FILE* file) -> std::errc {
    auto ec = std::errc::inappropriate_io_control_operation;
    if (isTerminal(file)) {
#if !defined(_WIN32) || defined(__linux__)
        ec = {};
#elif defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
        if (file == stdout || file == stderr) {
            auto  hOut   = GetStdHandle(file == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            DWORD dwMode = 0;
            ec           = std::errc::function_not_supported;
            if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode) &&
                SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                ec = {};
            }
        }
#else
        ec = std::errc::function_not_supported;
#endif
    }
    else if (isCygPty(file)) {
        ec = {};
    }
    return ec;
}

#if FLT_EVAL_METHOD == 2 || FLT_EVAL_METHOD < 0
#if defined(_MSC_VER) || defined(_WIN32)
#pragma fenv_access(on)
#endif
static unsigned setFpuPrecision(unsigned* r) {
#if defined(_FPU_GETCW) && defined(_FPU_SETCW) && defined(_FPU_DOUBLE)
    fpu_control_t cw;
    _FPU_GETCW(cw);
    if (r && cw == static_cast<fpu_control_t>(*r)) {
        return *r;
    }
    auto nw = static_cast<fpu_control_t>(
        not r ? (cw & ~static_cast<fpu_control_t>(_FPU_EXTENDED) & ~static_cast<fpu_control_t>(_FPU_SINGLE)) |
                    static_cast<fpu_control_t>(_FPU_DOUBLE)
              : static_cast<fpu_control_t>(*r));
    _FPU_SETCW(nw);
    return static_cast<unsigned>(cw);
#elif defined(_MSC_VER) || defined(_WIN32)
    unsigned cw = _controlfp(0, 0);
    unsigned nw = not r ? static_cast<unsigned>(_PC_53) : *r;
    _controlfp(nw, _MCW_PC);
    return cw;
#else
    return UINT32_MAX;
#endif
    return 0u;
}
#else
constexpr unsigned setFpuPrecision(unsigned*) { return 0u; }
#endif

namespace Potassco {
using namespace std::literals;

static constexpr auto c_file = std::string_view{__FILE__};

unsigned  initFpuPrecision() { return setFpuPrecision(nullptr); }
void      restoreFpuPrecision(unsigned r) { setFpuPrecision(&r); }
bool      isTerminal(FILE* file) { return ::isTerminal(file); }
std::errc enableAnsiColorSupport(FILE* file) { return enableTerminalColors(file); }
void      lockFile(FILE* file) { flockfile(file); }
void      unlockFile(FILE* file) { funlockfile(file); }

const char* ExpressionInfo::relativeFileName(const std::source_location& loc) {
    auto res = loc.file_name();
    for (auto cmp = c_file; *res && not cmp.empty() && *res == cmp.front(); cmp.remove_prefix(1), ++res) { ; }
    return res;
}

constinit AbortHandler g_abort_handler = nullptr;
extern AbortHandler    setAbortHandler(AbortHandler handler) { return std::exchange(g_abort_handler, handler); }
using ErrorBuffer = BasicCharBufferT<1024>;

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
