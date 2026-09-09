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
#include <potassco/format.h>
#include <potassco/utils.h>

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

auto parseChar(std::string_view in, char& out) -> std::from_chars_result {
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

auto parseUnsigned(std::string_view in, std::uintmax_t& out, std::uintmax_t max) -> std::from_chars_result {
    skipws(in);
    if (in.starts_with('-')) {
        if (not in.starts_with("-1")) {
            return Parse::error(in);
        }
        out = max;
        return Parse::success(in, 2);
    }

    if (bool isSignedMax = in.starts_with("imax"); isSignedMax || in.starts_with("umax")) {
        out = isSignedMax ? max >> 1u : max;
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

auto parseSigned(std::string_view in, std::intmax_t& out, std::intmax_t min,
                 std::intmax_t max) -> std::from_chars_result {
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
static auto parseFloatImpl(std::string_view in, T& out) -> std::from_chars_result {
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
            auto extract(std::string_view& inView, double& d) -> std::from_chars_result {
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

auto parseFloat(std::string_view in, double& out, double min, double max) -> std::from_chars_result {
    skipws(in);
    Parse::matchOpt(in, '+');
    auto r = parseFloatImpl(in, out);
    if (Parse::ok(r) && (out < min || out > max)) {
        r.ec = std::errc::result_out_of_range;
    }
    return r;
}

auto writeSigned(char* first, char* last, std::intmax_t in) -> char* {
    auto r = std::to_chars(first, last, in);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert signed integer %zd",
                   static_cast<std::ptrdiff_t>(in));
    return r.ptr;
}

auto writeUnsigned(char* first, char* last, std::uintmax_t in) -> char* {
    auto r = std::to_chars(first, last, in);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert unsigned integer %zu",
                   static_cast<size_t>(in));
    return r.ptr;
}

auto writeFloat(char* first, char* last, double in, int p) -> char* {
    auto fmt = std::chars_format::fixed;
    if (p <= 0) { // Set precision = 6 to match the default behavior of (s)printf.
        p   = 6;
        fmt = std::chars_format::general;
    }
    auto r = std::to_chars(first, last, in, fmt, p);
    POTASSCO_CHECK(r.ec == std::errc{}, r.ec, "std::to_chars could not convert double %g", in);
    return r.ptr;
}

auto resetStyle() -> std::string_view { return TextStyle::ts_reset_v; }

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

auto fromChars(std::string_view in, bool& out) -> std::from_chars_result {
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
/////////////////////////////////////////////////////////////////////////////////////////
// BasicCharBuffer
/////////////////////////////////////////////////////////////////////////////////////////
BasicCharBuffer::BasicCharBuffer(const BasicCharBuffer& other) { // NOLINT
    initState(other);
    append(other.view());
}
BasicCharBuffer::BasicCharBuffer(BasicCharBuffer&& other) noexcept { // NOLINT
    initState(other);
    if (other.small()) {
        append(other.view());
    }
    else {
        new (storage_) Large{*other.large()};
        storage_[max_small] = static_cast<char>(max_small + 1);
    }
    other.initState();
}
void BasicCharBuffer::clear() noexcept {
    if (small()) {
        initState();
    }
    else {
        storage_[term_byte] = 0;
        storage_[ts_byte]   = 0;
        large()->size       = 0;
    }
}
void BasicCharBuffer::release() noexcept {
    auto* l = large();
    SystemAllocator::deallocate(l->data, l->cap + 1);
}
auto BasicCharBuffer::operator=(const BasicCharBuffer& other) -> BasicCharBuffer& {
    if (this != &other) {
        clear();
        storage_[term_byte] = other.storage_[term_byte];
        storage_[ts_byte]   = other.storage_[ts_byte];
        append(other.view());
    }
    return *this;
}
auto BasicCharBuffer::operator=(BasicCharBuffer&& other) noexcept -> BasicCharBuffer& { // NOLINT
    if (this != &other) {
        if (not small()) {
            release();
        }
        initState(other);
        if (other.small()) {
            append(other.view());
        }
        else {
            new (storage_) Large{*other.large()};
            storage_[max_small] = static_cast<char>(max_small + 1);
        }
        other.initState();
    }
    return *this;
}
void BasicCharBuffer::setSize(uint32_t ns) {
    POTASSCO_DEBUG_ASSERT(ns <= capacity());
    if (small()) {
        POTASSCO_DEBUG_ASSERT(ns <= max_small);
        storage_[ns]        = 0;
        storage_[max_small] = static_cast<char>(max_small - ns);
    }
    else {
        auto* l     = large();
        l->size     = ns;
        l->data[ns] = 0;
    }
}
void BasicCharBuffer::pop(uint32_t n) noexcept {
    auto sz = size();
    POTASSCO_DEBUG_ASSERT(n <= sz);
    auto ns = sz - n;
    setSize(ns);
}

auto BasicCharBuffer::expand(std::size_t n, bool commit) -> char* {
    auto  sz  = size();
    auto  ns  = safe_cast<uint32_t>(sz + n + 1) - 1;
    char* out = nullptr;
    if (ns > capacity()) {
        auto nc = Detail::goodNextSize(std::max(1023u, ns) + 1, capacity(), 1u);
        if (not small()) {
            out      = static_cast<char*>(SystemAllocator::reallocate(large()->data, nc));
            *large() = Large{out, sz, nc - 1};
        }
        else {
            out = static_cast<char*>(SystemAllocator::reallocate(nullptr, nc));
            std::memcpy(out, storage_, sz);
            new (storage_) Large{out, sz, nc - 1};
            storage_[max_small] = static_cast<char>(max_small + 1);
        }
        POTASSCO_DEBUG_ASSERT(not small());
    }
    else if (not small()) {
        out = large()->data;
    }
    else {
        out = storage_;
    }
    if (commit) { // NOLINT
        setSize(ns);
    }
    return out + sz;
}
void BasicCharBuffer::appendImpl(std::string_view s) {
    if (not s.empty()) {
        auto* p = expand(s.size(), true);
        std::memcpy(p, s.data(), s.size());
    }
}
void BasicCharBuffer::push_back(char c) { *expand(1u, true) = c; }
void BasicCharBuffer::appendImpl(std::size_t n, char c) {
    auto* p = expand(n, true);
    std::memset(p, c, n);
}
auto BasicCharBuffer::appendForOverwrite(std::size_t n) -> std::span<char> { return {expand(n, true), n}; }
void BasicCharBuffer::writeField(const Field& f) {
    char             temp[128];
    char*            ep = std::end(temp);
    std::string_view s;
    switch (f.prec) {
        case Field::str_field : s = f.f.s; break;
        case Field::int_field : s = {temp, Detail::writeSigned(temp, ep, f.f.i)}; break;
        case Field::uint_field: s = {temp, Detail::writeUnsigned(temp, ep, f.f.u)}; break;
        default               : s = {temp, Detail::writeFloat(temp, ep, f.f.d, f.prec)}; break;
    }
    auto  w   = s.size() + (f.term != 0);
    auto  lf  = std::cmp_greater(f.width, w) ? static_cast<std::size_t>(f.width) - w : 0;
    auto  rf  = std::cmp_greater(-f.width, w) ? static_cast<std::size_t>(-f.width) - w : 0;
    auto* oIt = std::fill_n(expand(w + lf + rf, true), lf, ' ');
    oIt       = std::copy_n(s.data(), s.size(), oIt);
    if (f.term != 0) {
        *oIt++ = f.term;
    }
    std::fill_n(oIt, rf, ' ');
}

auto BasicCharBuffer::vFormatTo(const char* fmt, va_list args) noexcept -> std::size_t {
    POTASSCO_ASSERT(fmt);
    auto commit  = 0u;
    auto sz      = size();
    auto request = static_cast<std::size_t>(capacity() - sz);
    for (va_list saved;;) {
        va_copy(saved, args);
        POTASSCO_SCOPE_EXIT({ va_end(saved); });
        try {
            // NB: Our buffer always has room for a null-terminator.
            // We therefore can pass request + 1 to vsnprintf.
            auto* out = expand(request, false);
            auto  n   = std::vsnprintf(out, request + 1, fmt, saved);
            if (std::cmp_less(n, request + 1)) {
                commit = n > 0 ? static_cast<uint32_t>(n) : 0u;
                break;
            }
            request = static_cast<std::size_t>(n);
            if (storage_[max_small] == 0) {
                setSize(sz); // restore old size, which was overwritten by vsnprintf's null-terminator
            }
        }
        catch (const std::exception&) {
            // allocation error - truncate result
            break;
        }
    }
    if (commit) {
        POTASSCO_DEBUG_ASSERT(size() == sz || size() == sz + commit);
        auto ns = sz + commit;
        setSize(ns);
        POTASSCO_DEBUG_ASSERT(buf()[ns] == 0);
    }
    return commit;
}

} // namespace Potassco
