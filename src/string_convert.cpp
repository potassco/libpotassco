//
// Copyright (c) 2017 - present, Benjamin Kaufmann
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
#include <potassco/string_convert.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace std;

namespace Potassco {
namespace detail {
static int detectBase(std::string_view& x) {
    if (x.starts_with("0x") || x.starts_with("0X")) {
        x.remove_prefix(2);
        return 16;
    }
    else if (x.starts_with('0') && x.size() > 1 && x[1] >= '0' && x[1] <= '7') {
        x.remove_prefix(2);
        return 8;
    }
    else {
        return 10;
    }
}

std::from_chars_result error(std::string_view x, std::errc ec) { return {.ptr = x.data(), .ec = ec}; }
std::from_chars_result success(std::string_view x, std::size_t pop) {
    x.remove_prefix(pop);
    return {.ptr = x.data(), .ec = {}};
}

static void skipws(std::string_view& in) {
    auto p = in.find_first_not_of(" \f\n\r\t\v"sv);
    return in.remove_prefix(std::min(p, in.size()));
}

std::from_chars_result parseChar(std::string_view in, unsigned char& out) {
    static constexpr auto c_from = "fnrtv"sv;
    static constexpr auto c_to   = "\f\n\r\t\v"sv;

    if (in.empty())
        return detail::error(in);

    std::string_view::size_type pos;
    if (in.size() > 1 && in.front() == '\\' && (pos = c_from.find(in[1])) != std::string_view::npos) {
        out = static_cast<unsigned char>(c_to[pos]);
        return success(in, 2);
    }

    out = static_cast<unsigned char>(in[0]);
    return success(in, 1);
}

std::from_chars_result parseUnsigned(std::string_view in, std::uintmax_t& out, std::uintmax_t max) {
    skipws(in);
    if (in.starts_with('-')) {
        if (not in.starts_with("-1"))
            return error(in);
        out = max;
        return success(in, 2);
    }

    if (bool isSignedMax = in.starts_with("imax"); isSignedMax || in.starts_with("umax")) {
        out = isSignedMax ? max >> 1 : max;
        return success(in, 4);
    }

    matchOpt(in, '+');

    auto base = detectBase(in);

    if (in.empty())
        return error(in);

    auto r = std::from_chars(in.data(), in.data() + in.size(), out, base);
    if (r.ec == std::errc{} && out > max) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

std::from_chars_result parseSigned(std::string_view in, std::intmax_t& out, std::intmax_t min, std::intmax_t max) {
    skipws(in);
    if (bool isMax = in.starts_with("imax"); isMax || in.starts_with("imin")) {
        out = isMax ? max : min;
        return success(in, 4);
    }

    matchOpt(in, '+');

    auto base = detectBase(in);

    if (in.empty())
        return error(in);

    auto r = std::from_chars(in.data(), in.data() + in.size(), out, base);
    if (r.ec == std::errc{} && (out < min || out > max)) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

std::from_chars_result parseFloat(std::string_view in, double& out, double min, double max) {
    skipws(in);
    matchOpt(in, '+');
    auto r = std::from_chars(in.data(), in.data() + in.size(), out);
    if (r.ec == std::errc{} && (out < min || out > max)) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

char* writeSigned(char* first, char* last, std::intmax_t in) {
    auto r = std::to_chars(first, last, in);
    POTASSCO_EXPECT(r.ec == std::errc{}, "to chars failed with error %d", static_cast<int>(r.ec));
    return r.ptr;
}

char* writeUnsigned(char* first, char* last, std::uintmax_t in) {
    auto r = std::to_chars(first, last, in);
    POTASSCO_EXPECT(r.ec == std::errc{}, "to chars failed with error %d", static_cast<int>(r.ec));
    return r.ptr;
}

char* writeFloat(char* first, char* last, double in) {
    auto r = std::to_chars(first, last, in, std::chars_format::general);
    POTASSCO_EXPECT(r.ec == std::errc{}, "to chars failed with error %d", static_cast<int>(r.ec));
    return r.ptr;
}

bool matchOpt(std::string_view& in, char v) {
    if (in.starts_with(v)) {
        in.remove_prefix(1);
        return true;
    }
    return false;
}

} // namespace detail

std::from_chars_result fromChars(std::string_view in, bool& out) {
    if (in.empty())
        return detail::error(in);

    if (in.starts_with('0') || in.starts_with('1')) {
        out = in[0] == '1';
        return detail::success(in, 1);
    }
    else if (in.starts_with("no") || in.starts_with("on")) {
        out = in[0] == 'o';
        return detail::success(in, 2);
    }
    else if (in.starts_with("off") || in.starts_with("yes")) {
        out = in[0] == 'y';
        return detail::success(in, 3);
    }
    else if (in.starts_with("false") || in.starts_with("true")) {
        out = in[0] == 't';
        return detail::success(in, 4 + int(!out));
    }
    else {
        return detail::error(in);
    }
}

const char* bad_string_cast::what() const noexcept { return "bad_string_cast"; }

#define FAIL(MSG) (fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, MSG), std::abort())

static int vsnprintf(char* s, size_t n, const char* format, va_list arg) { return std::vsnprintf(s, n, format, arg); }

void fail(int ec, const char* file, unsigned line, const char* exp, const char* fmt, ...) {
    char            msg[1024];
    std::span<char> span(msg);

    auto append = [](std::span<char>& s, std::string_view view) {
        auto n = std::min(view.size(), s.size());
        std::copy(view.begin(), view.begin() + n, s.begin());
        s = s.subspan(n);
    };
    if (ec >= 0 || ec == error_assert) {
        if (file && line) {
            append(span, file);
            append(span, "@");
            auto r = std::to_chars(span.data(), span.data() + span.size(), line);
            span   = span.subspan(static_cast<size_t>(r.ptr - span.data()));
            append(span, ": ");
        }
        append(span, ec > 0 ? strerror(ec) : ec == error_assert ? "assertion failure" : "error code must not be 0");
        append(span, ": ");
    }
    else if (!fmt) {
        append(span, ec == error_logic ? "logic" : "runtime");
        append(span, " error ");
    }
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        if (auto r = vsnprintf(span.data(), span.size(), fmt, args); r > 0)
            span = span.subspan(static_cast<size_t>(std::min(r, static_cast<int>(span.size()))));
        va_end(args);
    }
    else if (exp) {
        append(span, "check('");
        append(span, exp);
        append(span, "') failed");
    }

    if (not span.empty())
        span.front() = 0;
    else
        msg[std::size(msg) - 1] = 0;

    switch (ec) {
        case error_logic: // fallthrough
        case error_assert : throw std::logic_error(msg);
        case error_runtime: throw std::runtime_error(msg);
        case ENOMEM       : throw std::bad_alloc();
        case 0            : // fallthrough
        case EINVAL       : throw std::invalid_argument(msg);
        case EDOM         : throw std::domain_error(msg);
        case ERANGE       : throw std::range_error(msg);
#if defined(EOVERFLOW)
        case EOVERFLOW: throw std::overflow_error(msg);
#endif
        default: throw std::runtime_error(msg);
    }
}

} // namespace Potassco
