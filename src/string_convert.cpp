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
#include <potassco/program_opts/string_convert.h>

#include <potassco/error.h>

#if not defined(_MSC_VER)
#include <strings.h>
#else
inline int strcasecmp(const char* lhs, const char* rhs) { return _stricmp(lhs, rhs); }
inline int strncasecmp(const char* lhs, const char* rhs, size_t n) { return _strnicmp(lhs, rhs, n); }
#endif

#include <sstream>

using namespace std;

namespace Potassco {
namespace Detail {
static constexpr int detectBase(std::string_view& x) {
    if (x.starts_with("0x") || x.starts_with("0X")) {
        x.remove_prefix(2);
        return 16;
    }
    if (x.starts_with('0') && x.size() > 1 && x[1] >= '0' && x[1] <= '7') {
        x.remove_prefix(2);
        return 8;
    }
    return 10;
}

static constexpr void skipws(std::string_view& in) {
    auto p = in.find_first_not_of(" \f\n\r\t\v"sv);
    return in.remove_prefix(std::min(p, in.size()));
}

std::from_chars_result parseChar(std::string_view in, unsigned char& out) {
    static constexpr auto c_from = "fnrtv"sv;
    static constexpr auto c_to   = "\f\n\r\t\v"sv;

    if (in.empty()) {
        return Parse::error(in);
    }

    if (auto pos = in[0] == '\\' ? c_from.find(in[in.size() > 1]) : std::string_view::npos; pos < c_to.size()) {
        out = static_cast<unsigned char>(c_to[pos]);
        return Parse::success(in, 2);
    }

    out = static_cast<unsigned char>(in[0]);
    return Parse::success(in, 1);
}

std::from_chars_result parseUnsigned(std::string_view in, std::uintmax_t& out, std::uintmax_t max) {
    skipws(in);
    if (in.starts_with('-')) {
        if (not in.starts_with("-1")) {
            return Parse::error(in);
        }
        out = max;
        return Parse::success(in, 2);
    }

    if (bool isSignedMax = in.starts_with("imax"); isSignedMax || in.starts_with("umax")) {
        out = isSignedMax ? max >> 1 : max;
        return Parse::success(in, 4);
    }

    Parse::matchOpt(in, '+');

    auto base = detectBase(in);

    if (in.empty()) {
        return Parse::error(in);
    }

    auto r = std::from_chars(in.data(), in.data() + in.size(), out, base);
    if (Parse::ok(r) && out > max) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

std::from_chars_result parseSigned(std::string_view in, std::intmax_t& out, std::intmax_t min, std::intmax_t max) {
    skipws(in);
    if (bool isMax = in.starts_with("imax"); isMax || in.starts_with("imin")) {
        out = isMax ? max : min;
        return Parse::success(in, 4);
    }

    Parse::matchOpt(in, '+');

    auto base = detectBase(in);

    if (in.empty()) {
        return Parse::error(in);
    }

    auto r = std::from_chars(in.data(), in.data() + in.size(), out, base);
    if (Parse::ok(r) && (out < min || out > max)) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

template <typename T = double>
std::from_chars_result parseFloatImpl(std::string_view in, T& out) {
    if constexpr (requires { std::from_chars(in.data(), in.data() + in.size(), out); }) {
        return std::from_chars(in.data(), in.data() + in.size(), out);
    }
    else {
        struct ViewStream
            : private std::streambuf
            , private std::istream {
            explicit ViewStream() : std::streambuf(), std::istream(static_cast<std::streambuf*>(this)) {
                if (const auto& classic = std::locale::classic(); std::istream::getloc() != classic) {
                    std::istream::imbue(classic); // mimic from_chars, which is locale independent
                }
            }
            std::from_chars_result extract(std::string_view& inView, double& d) {
                for (auto cv = inView;;) {
                    auto* buf = const_cast<char*>(cv.data());
                    std::streambuf::setg(buf, buf, buf + std::ssize(cv));
                    auto ok  = static_cast<bool>((*this) >> d);
                    auto pos = static_cast<std::size_t>(gptr() - eback());
                    if (ok || pos == 0 || pos > cv.size()) {
                        return ok ? Parse::success(inView, std::min(pos, inView.size())) : Parse::error(inView);
                    }
                    // Some prefix was matched but not converted.
                    // NOTE: libc++ for example will fail to extract a double from "123.23Foo" while both strtod and
                    //       std::from_chars will extract "123.23" while leaving "Foo" in the input.
                    cv = cv.substr(0, pos - 1);
                    clear();
                }
            }
        };
        return ViewStream{}.extract(in, out);
    }
}

std::from_chars_result parseFloat(std::string_view in, double& out, double min, double max) {
    skipws(in);
    Parse::matchOpt(in, '+');
    auto r = parseFloatImpl(in, out);
    if (Parse::ok(r) && (out < min || out > max)) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

char* writeSigned(char* first, char* last, std::intmax_t in) {
    auto r = std::to_chars(first, last, in);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert signed integer %zd",
                   static_cast<std::ptrdiff_t>(in));
    return r.ptr;
}

char* writeUnsigned(char* first, char* last, std::uintmax_t in) {
    auto r = std::to_chars(first, last, in);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert unsigned integer %zu",
                   static_cast<size_t>(in));
    return r.ptr;
}

char* writeFloat(char* first, char* last, double in) {
    // Set precision = 6 to match default behavior of (s)printf.
    auto r = std::to_chars(first, last, in, std::chars_format::general, 6);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert double %g", in);
    return r.ptr;
}

} // namespace Detail
namespace Parse {
bool eqIgnoreCase(const char* lhs, const char* rhs, std::size_t n) { return strncasecmp(lhs, rhs, n) == 0; }
bool eqIgnoreCase(const char* lhs, const char* rhs) { return strcasecmp(lhs, rhs) == 0; }
bool matchOpt(std::string_view& in, char v) {
    if (in.starts_with(v)) {
        in.remove_prefix(1);
        return true;
    }
    return false;
}

} // namespace Parse

std::from_chars_result fromChars(std::string_view in, bool& out) {
    if (in.empty()) {
        return Parse::error(in);
    }
    if (in.starts_with('0') || in.starts_with('1')) {
        out = in[0] == '1';
        return Parse::success(in, 1);
    }
    if (in.starts_with("no") || in.starts_with("on")) {
        out = in[0] == 'o';
        return Parse::success(in, 2);
    }
    if (in.starts_with("off") || in.starts_with("yes")) {
        out = in[0] == 'y';
        return Parse::success(in, 3);
    }
    if (in.starts_with("false") || in.starts_with("true")) {
        out = in[0] == 't';
        return Parse::success(in, 4u + static_cast<unsigned>(not out));
    }
    return Parse::error(in);
}

} // namespace Potassco
