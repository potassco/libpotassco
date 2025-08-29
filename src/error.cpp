#include <potassco/error.h>

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
#include <charconv>
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

static void append(std::span<char>& s, std::string_view view) {
    auto n = std::min(view.size(), s.size());
    std::copy_n(view.begin(), n, s.begin());
    s = s.subspan(n);
}

static void appendExpression(std::span<char>& span, const ExpressionInfo& info, bool addFile, std::string_view type,
                             bool more) {
    append(span, addFile ? ExpressionInfo::relativeFileName(info.location) : info.location.function_name());
    append(span, ":"sv);
    auto r = std::to_chars(span.data(), span.data() + span.size(), info.location.line());
    span   = span.subspan(static_cast<size_t>(r.ptr - span.data()));
    if (addFile) {
        append(span, ": "sv);
        append(span, info.location.function_name());
    }
    append(span, ": "sv);
    if (not type.empty()) {
        append(span, type);
        if (not info.expression.empty()) {
            append(span, " '"sv);
            append(span, info.expression);
            append(span, "'"sv);
        }
        append(span, " "sv);
    }
    append(span, "failed."sv);
    if (more) {
        append(span, "\nmessage: "sv);
    }
}

constinit AbortHandler g_abortHandler = nullptr;
extern AbortHandler    setAbortHandler(AbortHandler handler) { return std::exchange(g_abortHandler, handler); }

extern void failAbort(const ExpressionInfo& expressionInfo, const char* fmt, ...) {
    char      message[1024];
    std::span span(message, std::size(message) - 1);
    auto      hasMessage = fmt && *fmt;
    appendExpression(span, expressionInfo, true, "Assertion"sv, hasMessage);
    if (hasMessage) {
        va_list args;
        va_start(args, fmt);
        if (auto r = std::vsnprintf(span.data(), span.size(), fmt, args); r > 0) {
            span = span.subspan(static_cast<size_t>(std::min(r, static_cast<int>(span.size()))));
        }
        va_end(args);
    }
    *span.data() = 0;
    if (g_abortHandler) {
        g_abortHandler(message);
    }
    fprintf(stderr, "%s\n", message);
    std::abort();
}

extern void failThrow(Errc ec, const ExpressionInfo& expressionInfo, const char* fmt, ...) {
    if (ec == Errc::bad_alloc) {
        throw std::bad_alloc();
    }

    char      message[1024];
    std::span span(message, std::size(message) - 1);
    auto      hasMessage = fmt && *fmt;
    if (ec == Errc::precondition_fail) {
        appendExpression(span, expressionInfo, false, "Precondition"sv, hasMessage);
    }
    auto next = std::string_view{};
    if (hasMessage) {
        va_list args;
        va_start(args, fmt);
        if (auto r = std::vsnprintf(span.data(), span.size(), fmt, args); r > 0) {
            span = span.subspan(static_cast<size_t>(std::min(r, static_cast<int>(span.size()))));
        }
        va_end(args);
        next = ": "sv;
    }
    if (ec == Errc::precondition_fail) {
        *span.data() = 0;
        throw std::invalid_argument(message);
    }
    append(span, next);
    append(span, std::generic_category().message(static_cast<int>(ec)));
    append(span, "\n");
    appendExpression(span, expressionInfo, false, expressionInfo.expression.empty() ? ""sv : "check"sv, false);
    *span.data() = 0;
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
