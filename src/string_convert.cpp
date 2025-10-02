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

#include <potassco/basic_types.h>
#include <potassco/error.h>
#include <potassco/format.h>

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

std::from_chars_result parseChar(std::string_view in, char& out) {
    static constexpr auto c_from = "fnrtv"sv;
    static constexpr auto c_to   = "\f\n\r\t\v"sv;

    if (in.empty()) {
        return Parse::error(in);
    }

    if (auto pos = in[0] == '\\' ? c_from.find(in[in.size() > 1]) : std::string_view::npos; pos < c_to.size()) {
        out = c_to[pos];
        return Parse::success(in, 2);
    }

    out = in[0];
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
                    // NOTE: libc++, for example, will fail to extract a double from "123.23Foo" while both strtod and
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

char* writeFloat(char* first, char* last, double in, int p) {
    auto fmt = std::chars_format::fixed;
    if (p <= 0) { // Set precision = 6 to match the default behavior of (s)printf.
        p   = 6;
        fmt = std::chars_format::general;
    }
    auto r = std::to_chars(first, last, in, fmt, p);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert double %g", in);
    return r.ptr;
}

void writeField(DynamicBuffer& buffer, const Field& f) {
    char             temp[128];
    char*            ep = std::end(temp);
    std::string_view s;
    switch (f.prec) {
        case Field::str_field : s = f.f.s; break;
        case Field::int_field : s = {temp, writeSigned(temp, ep, f.f.i)}; break;
        case Field::uint_field: s = {temp, writeUnsigned(temp, ep, f.f.u)}; break;
        default               : s = {temp, writeFloat(temp, ep, f.f.d, f.prec)}; break;
    }
    auto  w   = s.size() + (f.term != 0);
    auto  lf  = std::cmp_greater(f.width, w) ? static_cast<std::size_t>(f.width) - w : 0;
    auto  rf  = std::cmp_greater(-f.width, w) ? static_cast<std::size_t>(-f.width) - w : 0;
    auto* oIt = std::fill_n(buffer.alloc(w + lf + rf).data(), lf, ' ');
    oIt       = std::copy_n(s.data(), s.size(), oIt);
    if (f.term != 0) {
        *oIt++ = f.term;
    }
    std::fill_n(oIt, rf, ' ');
}

auto resetStyle() -> std::string_view { return TextStyle::ts_reset_v; }

auto vFormatTo(DynamicBuffer& buffer, const char* fmt, va_list args) noexcept -> std::size_t {
    bool truncate = false;
    for (va_list saved;;) {
        va_copy(saved, args);
        POTASSCO_SCOPE_EXIT({ va_end(saved); });
        auto avail = buffer.alloc(buffer.capacity() - buffer.size());
        auto n     = std::vsnprintf(avail.data(), avail.size(), fmt, saved);
        if (n < 0) {
            return 0;
        }
        if (static_cast<std::size_t>(n) < avail.size()) {
            buffer.pop(avail.size() - static_cast<std::size_t>(n));
            return static_cast<std::size_t>(n);
        }
        if (truncate) {
            return avail.size();
        }
        try {
            buffer.pop(avail.size());
            buffer.reserve(buffer.size() + static_cast<std::size_t>(n + 1));
        }
        catch (const std::exception&) {
            // allocation error - truncate result
            truncate = true;
        }
    }
}

} // namespace Detail
namespace Parse {
bool eqIgnoreCase(std::string_view lhs, std::string_view rhs) {
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    return lhs.size() == rhs.size() && (lhs.empty() || strncasecmp(lhs.data(), rhs.data(), lhs.size()) == 0);
}
bool eqIgnoreCase(std::string_view lhs, std::string_view rhs, std::size_t n) {
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    return lhs.size() >= n && rhs.size() >= n && (not n || strncasecmp(lhs.data(), rhs.data(), n) == 0);
}

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

auto TextStyle::Spec::fromString(std::string_view str, std::string_view::size_type startPos) -> Spec {
    POTASSCO_CHECK_PRE(startPos <= str.size(), "startPos out of range");
    auto style = str;
    str        = style.substr(startPos);
    Spec ret{};
    while (not str.empty()) {
        uint8_t val    = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
        POTASSCO_CHECK(ec == std::errc{}, ec, "invalid number in '%" PRIsv "'", PRI_SV(style));
        if (val < 30) {
            POTASSCO_CHECK(ret.em == Emphasis::none, std::errc::invalid_argument, "duplicate emphasis in '%" PRIsv "'",
                           PRI_SV(style));
            POTASSCO_CHECK(val <= to_underlying(Emphasis::underline), Potassco::Errc::domain_error,
                           "invalid emphasis in '%" PRIsv "'", PRI_SV(style));
            ret.em = static_cast<Emphasis>(val);
        }
        else {
            auto bg  = val == 49 || (val > 40 && val < 48) || (val > 100 && val < 108) ? 10u : 0u;
            auto off = ((val >= 90 ? 81u : 29u) * (val != (39 + bg))) + bg;
            auto tc  = static_cast<Color>(val - off);
            POTASSCO_CHECK(tc == TextStyle::Color::def ||
                               (tc >= TextStyle::Color::black && tc <= TextStyle::Color::bright_white),
                           Potassco::Errc::domain_error, "invalid terminal color  in '%" PRIsv "'", PRI_SV(style));
            if (bg) {
                POTASSCO_CHECK(std::exchange(ret.bg, tc) == Color{}, std::errc::invalid_argument,
                               "duplicate bg color in '%" PRIsv "'", PRI_SV(style));
            }
            else {
                POTASSCO_CHECK(std::exchange(ret.fg, tc) == Color{}, std::errc::invalid_argument,
                               "duplicate fg color in '%" PRIsv "'", PRI_SV(style));
            }
        }
        str.remove_prefix(static_cast<std::string_view::size_type>(ptr - str.data()));
        if (str.starts_with(';') && str.size() > 1) {
            str.remove_prefix(1);
        }
    }
    return ret;
}
auto TextStyle::fromString(std::string_view str, std::string_view::size_type startPos) -> TextStyle {
    auto spec = Spec::fromString(str, startPos);
    return spec != Spec{} ? TextStyle(spec) : TextStyle();
}

} // namespace Potassco
