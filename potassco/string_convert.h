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
#ifndef POTASSCO_STRING_CONVERT_H_INCLUDED
#define POTASSCO_STRING_CONVERT_H_INCLUDED
#include <potassco/platform.h>

#include <potassco/basic_types.h>
#include <potassco/enum.h>

#include <charconv>
#include <cinttypes>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_MSC_VER)
#include <strings.h>
#else
inline int strcasecmp(const char* lhs, const char* rhs) { return _stricmp(lhs, rhs); }
inline int strncasecmp(const char* lhs, const char* rhs, size_t n) { return _strnicmp(lhs, rhs, n); }
#endif
namespace Potassco {
namespace detail {
std::from_chars_result parseChar(std::string_view in, unsigned char& out);
std::from_chars_result parseUnsigned(std::string_view in, std::uintmax_t& out, std::uintmax_t max);
std::from_chars_result parseSigned(std::string_view in, std::intmax_t& out, std::intmax_t min, std::intmax_t max);
std::from_chars_result parseFloat(std::string_view in, double& out, double min, double max);
std::from_chars_result error(std::string_view x, std::errc ec = std::errc::invalid_argument);
std::from_chars_result success(std::string_view x, std::size_t pop);
char*                  writeSigned(char* first, char* last, std::intmax_t);
char*                  writeUnsigned(char* first, char* last, std::uintmax_t);
char*                  writeFloat(char* first, char* last, double);
bool                   matchOpt(std::string_view& in, char v);
} // namespace detail
///////////////////////////////////////////////////////////////////////////////
// chars -> T
///////////////////////////////////////////////////////////////////////////////
std::from_chars_result fromChars(std::string_view in, bool& out);
template <std::integral T>
requires(not std::is_same_v<T, bool>)
std::from_chars_result fromChars(std::string_view in, T& out) {
    std::from_chars_result res{};
    if constexpr (std::is_unsigned_v<T>) {
        std::uintmax_t temp;
        if (res = detail::parseUnsigned(in, temp, std::numeric_limits<T>::max()); res.ec == std::errc{})
            out = static_cast<T>(temp);
    }
    else {
        std::intmax_t temp;
        if (res = detail::parseSigned(in, temp, std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
            res.ec == std::errc{})
            out = static_cast<T>(temp);
    }
    if (sizeof(T) == 1 && res.ec != std::errc{}) {
        unsigned char temp;
        if (res = detail::parseChar(in, temp); res.ec == std::errc{})
            out = static_cast<T>(temp);
    }
    return res;
}
template <std::floating_point T>
std::from_chars_result fromChars(std::string_view in, T& out) {
    double temp;
    auto   r = detail::parseFloat(in, temp, std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    if (r.ec == std::errc{})
        out = static_cast<T>(temp);
    return r;
}
inline std::from_chars_result fromChars(std::string_view in, std::string& out) {
    out.append(in);
    return detail::success(in, in.size());
}
inline std::from_chars_result fromChars(std::string_view in, std::string_view& out) {
    out = in;
    return detail::success(in, in.size());
}

// Parses T[,U] optionally enclosed in parentheses.
// TODO: Why do we allow single values? This should be ok only if U is std::optional.
template <typename T, typename U>
std::from_chars_result fromChars(std::string_view in, std::pair<T, U>& out) {
    auto temp(out);
    bool m = detail::matchOpt(in, '(');
    auto r = fromChars(in, temp.first);
    if (r.ec != std::errc{})
        return r;
    in.remove_prefix(r.ptr - in.begin());
    if (not in.empty() && in[0] == ',') {
        in.remove_prefix(1);
        if (r = fromChars(in, temp.second); r.ec != std::errc{})
            return r;
        in.remove_prefix(r.ptr - in.begin());
    }
    if (m && not detail::matchOpt(in, ')'))
        return detail::error(in);
    out = std::move(temp);
    return detail::success(in, 0);
}

// parses T1 [, ..., Tn] optionally enclosed in brackets
template <typename C>
requires requires(C c, std::string_view in) {
    typename C::value_type;
    c.push_back(std::declval<typename C::value_type>());
}
std::from_chars_result fromChars(std::string_view in, C& out) {
    auto m = detail::matchOpt(in, '[');
    for (typename C::value_type temp; not in.empty();) {
        auto r = fromChars(in, temp);
        if (r.ec != std::errc{})
            return r;
        out.push_back(std::move(temp));
        in.remove_prefix(r.ptr - in.begin());
        if (in.size() < 2 || not detail::matchOpt(in, ','))
            break;
    }
    if (m && not detail::matchOpt(in, ']'))
        return detail::error(in);
    return detail::success(in, 0);
}

template <typename EnumT>
requires EnumReflect<EnumT>::value
std::from_chars_result fromChars(std::string_view in, EnumT& out) {
    // try numeric extraction first
    using UT = std::underlying_type_t<EnumT>;
    UT   v;
    auto ret = fromChars(in, v);
    if (ret.ec == std::errc{}) {
        if (enum_cast<EnumT>(v).has_value()) {
            out = static_cast<EnumT>(v);
        }
        else {
            ret = detail::error(in);
        }
    }
    else {
        // try extraction "by name"
        for (const auto& [key, val] : Potassco::enum_entries<EnumT>()) {
            if (auto n = val.size();
                n >= in.size() && strncasecmp(in.data(), val.data(), n) == 0 && (!in[n] || in[n] == ',')) {
                out = static_cast<EnumT>(key);
                ret = detail::success(in, n);
                break;
            }
        }
    }
    return ret;
}
///////////////////////////////////////////////////////////////////////////////
// T -> chars
///////////////////////////////////////////////////////////////////////////////
template <typename T>
concept CharBuffer = requires(T buffer, std::string_view v) {
    { buffer.append(v) } -> std::convertible_to<T&>;
};

template <CharBuffer S>
S& toChars(S& out, const char* in) {
    return out.append(in ? std::string_view(in) : std::string_view());
}
template <CharBuffer S>
S& toChars(S& out, const std::string& s) {
    return out.append(std::string_view(s));
}
template <CharBuffer S>
S& toChars(S& out, std::string_view s) {
    return out.append(s);
}
template <CharBuffer S>
S& toChars(S& out, bool b) {
    return out.append(std::string_view(b ? "true" : "false"));
}

template <CharBuffer S, std::integral T>
S& toChars(S& out, T in) {
    char  temp[128];
    char* end = temp;
    if constexpr (std::is_unsigned_v<T>) {
        if (in == static_cast<T>(-1)) {
            return out.append(std::string_view("umax"));
        }
        else {
            end = detail::writeUnsigned(std::begin(temp), std::end(temp), in);
        }
    }
    else {
        end = detail::writeSigned(std::begin(temp), std::end(temp), in);
    }
    return out.append(std::string_view{temp, end});
}
template <CharBuffer S, std::floating_point T>
S& toChars(S& out, T in) {
    char  temp[128];
    auto* end = detail::writeFloat(std::begin(temp), std::end(temp), static_cast<double>(in));
    return out.append(std::string_view{temp, end});
}
template <CharBuffer S, typename EnumT>
requires EnumReflect<EnumT>::value
S& toChars(S& out, EnumT enumT) {
    if (auto name = Potassco::enum_name(enumT); not name.empty())
        return out.append(name);
    else
        return toChars(out, static_cast<std::underlying_type_t<EnumT>>(enumT));
}

template <CharBuffer S, typename T, typename U>
S& toChars(S& out, const std::pair<T, U>& p, char sep = ',') {
    toChars(out, p.first).append(1, sep);
    return toChars(out, p.second);
}
template <CharBuffer S, typename C>
requires requires(S& s, C c) {
    c.begin();
    c.end();
    toChars(s, *c.begin());
}
S& toChars(S& out, const C& c, char sep = ',') {
    int n = 0;
    for (const auto& v : c) {
        out.append(std::string_view(&sep, std::exchange(n, 1)));
        toChars(out, v);
    }
    return out;
}

template <std::size_t N>
struct FormatString : std::string_view {
    template <typename StrT>
    consteval FormatString(const StrT& s) : std::string_view(s) {
        auto fmt = std::string_view(s);
        auto n   = std::size_t(0);
        for (std::string_view::size_type p = 0;;) {
            if (p = fmt.find("{}", p); p >= fmt.size())
                break;
            if (++n > N)
                Potassco::fail(EINVAL, nullptr, 0, nullptr, "too many arguments in format string");
            p += 2;
        }
    }
};

template <CharBuffer S, typename... Args>
constexpr S& formatTo(S& out, FormatString<sizeof...(Args)> fmt, const Args&... args) {
    using ArgFmt              = void (*)(S& s, const void*);
    using ArgPtr              = const void*;
    constexpr ArgFmt argFmt[] = {+[](S& s, ArgPtr a) { toChars(s, *static_cast<const Args*>(a)); }...};
    ArgPtr           argPtr[] = {&args...};

    auto n = std::size_t(0);
    for (std::string_view::size_type p = 0;; ++n) {
        auto ep = std::min(fmt.find("{}", p), fmt.size());
        out.append(fmt.substr(p, ep - p));
        if (ep == fmt.size())
            break;
        POTASSCO_REQUIRE(n < sizeof...(Args), "too many arguments in format string");
        argFmt[n](out, argPtr[n]);
        p = ep + 2;
    }
    return out;
}
template <typename... Args>
constexpr std::string formatToStr(FormatString<sizeof...(Args)> fmt, const Args&... args) {
    std::string out;
    formatTo(out, fmt, args...);
    return out;
}
///////////////////////////////////////////////////////////////////////////////
// string -> T
///////////////////////////////////////////////////////////////////////////////
class bad_string_cast : public std::bad_cast {
public:
    [[nodiscard]] const char* what() const noexcept override;
};
template <typename T>
bool string_cast(const char* arg, T& to) {
    std::string_view view(arg);
    auto             r = fromChars(view, to);
    return r.ec == std::errc{} && !*r.ptr;
}
template <typename T>
T string_cast(const char* s) {
    T to;
    if (string_cast<T>(s, to)) {
        return to;
    }
    throw bad_string_cast();
}
template <typename T>
T string_cast(const std::string& s) {
    return string_cast<T>(s.c_str());
}
template <typename T>
bool string_cast(const std::string& from, T& to) {
    return string_cast<T>(from.c_str(), to);
}

template <typename T>
bool stringTo(const char* str, T& x) {
    return string_cast(str, x);
}
///////////////////////////////////////////////////////////////////////////////
// T -> string
///////////////////////////////////////////////////////////////////////////////
template <typename U>
std::string string_cast(const U& num) {
    std::string out;
    toChars(out, num);
    return out;
}
template <typename T>
inline auto toString(const T& x) -> decltype(string_cast(x)) {
    return string_cast(x);
}
template <typename T, typename U>
inline std::string toString(const T& x, const U& y) {
    std::string res;
    toChars(res, x).append(1, ',');
    return toChars(res, y);
}
template <typename T, typename U, typename V>
std::string toString(const T& x, const U& y, const V& z) {
    std::string res;
    toChars(res, x).append(1, ',');
    toChars(res, y).append(1, ',');
    return toChars(res, z);
}

} // namespace Potassco

#endif
