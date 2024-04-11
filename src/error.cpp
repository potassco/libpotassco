#include <potassco/error.h>

#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

namespace Potassco {
using namespace std::literals;

static constexpr auto c_file = std::string_view{__FILE__};

const char* ExpressionInfo::relativeFileName(const std::source_location& loc) {
    auto res = loc.file_name();
    for (auto cmp = c_file; *res && not cmp.empty() && *res == cmp.front(); cmp.remove_prefix(1), ++res) { ; }
    return res;
}

static void append(std::span<char>& s, std::string_view view) {
    auto n = std::min(view.size(), s.size());
    std::copy(view.begin(), view.begin() + n, s.begin());
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
    if (more)
        append(span, "\nmessage: "sv);
}

constinit AbortHandler g_abortHandler = nullptr;
extern AbortHandler    setAbortHandler(AbortHandler handler) { return std::exchange(g_abortHandler, handler); }

extern void failAbort(const ExpressionInfo& info, const char* fmt, ...) {
    char            message[1024];
    std::span<char> span(message, std::size(message) - 1);
    auto            hasMessage = fmt && *fmt;
    appendExpression(span, info, true, "Assertion"sv, hasMessage);
    if (hasMessage) {
        va_list args;
        va_start(args, fmt);
        if (auto r = std::vsnprintf(span.data(), span.size(), fmt, args); r > 0)
            span = span.subspan(static_cast<size_t>(std::min(r, static_cast<int>(span.size()))));
        va_end(args);
    }
    *span.data() = 0;
    if (g_abortHandler)
        g_abortHandler(message);
    fprintf(stderr, "%s\n", message);
    std::abort();
}

extern void failThrow(Errc ec, const ExpressionInfo& info, const char* fmt, ...) {
    if (ec == Errc::bad_alloc)
        throw std::bad_alloc();

    char            message[1024];
    std::span<char> span(message, std::size(message) - 1);
    auto            hasMessage = fmt && *fmt;
    if (ec == Errc::precondition_fail) {
        appendExpression(span, info, false, "Precondition"sv, hasMessage);
    }
    auto next = std::string_view{};
    if (hasMessage) {
        va_list args;
        va_start(args, fmt);
        if (auto r = std::vsnprintf(span.data(), span.size(), fmt, args); r > 0)
            span = span.subspan(static_cast<size_t>(std::min(r, static_cast<int>(span.size()))));
        va_end(args);
        next = ": "sv;
    }
    if (ec == Errc::precondition_fail) {
        *span.data() = 0;
        throw std::invalid_argument(message);
    }
    append(span, next);
    append(span, std::generic_category().message(static_cast<int>(ec)));
    *span.data()       = 0;
    auto addExpression = [&]() {
        append(span, "\n");
        appendExpression(span, info, false, info.expression.empty() ? ""sv : "check"sv, false);
        *span.data() = 0;
    };
    switch (ec) {
        // logic
        case Errc::length_error    : addExpression(); throw std::length_error(message);
        case Errc::invalid_argument: addExpression(); throw std::invalid_argument(message);
        case Errc::domain_error    : addExpression(); throw std::domain_error(message);
        case Errc::out_of_range    : addExpression(); throw std::out_of_range(message);
        // runtime
        case Errc::overflow_error: addExpression(); throw std::overflow_error(message);
        default                  : throw RuntimeError(ec, info, message);
    }
}

std::string RuntimeError::details() const {
    std::string ret;
    ret.append("in ").append(location_.function_name()).append(":").append(std::to_string(location_.line()));
    if (not expression_.empty())
        ret.append(": check '").append(expression_).append("' failed");
    return ret;
}

} // namespace Potassco
