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
#pragma once

#include <potassco/platform.h>

#include <potassco/basic_types.h>
#include <potassco/enum.h>

#include <cassert>
#include <charconv>
#include <cinttypes>
#include <limits>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#if not defined(_MSC_VER)
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
std::from_chars_result error(std::string_view& x, std::errc ec = std::errc::invalid_argument);
std::from_chars_result success(std::string_view& x, std::size_t pop);
char*                  writeSigned(char* first, char* last, std::intmax_t);
char*                  writeUnsigned(char* first, char* last, std::uintmax_t);
char*                  writeFloat(char* first, char* last, double);
bool                   matchOpt(std::string_view& in, char v);
bool                   eqIgnoreCase(const char* lhs, const char* rhs, std::size_t n);

constexpr std::from_chars_result popSuccess(std::string_view& in, std::from_chars_result r) {
    if (r.ec == std::errc{}) {
        auto dist = r.ptr - in.data();
        assert(dist >= 0 && static_cast<std::size_t>(dist) <= in.size());
        in.remove_prefix(static_cast<std::size_t>(dist));
    }
    return r;
}
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
    if constexpr (sizeof(T) == 1) {
        if (res.ec != std::errc{}) {
            unsigned char temp;
            if (res = detail::parseChar(in, temp); res.ec == std::errc{})
                out = static_cast<T>(temp);
        }
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
    auto r = detail::popSuccess(in, fromChars(in, temp.first));
    if (r.ec != std::errc{})
        return r;
    if (not in.empty() && in[0] == ',') {
        in.remove_prefix(1);
        if (r = detail::popSuccess(in, fromChars(in, temp.second)); r.ec != std::errc{})
            return r;
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
        auto r = detail::popSuccess(in, fromChars(in, temp));
        if (r.ec != std::errc{})
            return r;
        out.push_back(std::move(temp));
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
            auto n   = val.size();
            auto res = in.size() >= n && detail::eqIgnoreCase(in.data(), val.data(), n);
            if (res && (n == in.size() || in[n] == ',')) {
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
    std::size_t n = 0;
    for (const auto& v : c) {
        out.append(std::string_view(&sep, std::exchange(n, 1)));
        toChars(out, v);
    }
    return out;
}

///////////////////////////////////////////////////////////////////////////////
// string -> T
///////////////////////////////////////////////////////////////////////////////
template <typename T>
std::errc stringTo(const char* arg, T& to) {
    std::string_view view(arg);
    if (auto r = fromChars(view, to); r.ec != std::errc{})
        return r.ec;
    else
        return !*r.ptr ? std::errc{} : std::errc::invalid_argument;
}

template <typename T>
std::errc stringTo(const std::string& str, T& x) {
    return stringTo(str.c_str(), x);
}
///////////////////////////////////////////////////////////////////////////////
// T -> string
///////////////////////////////////////////////////////////////////////////////
template <typename T>
requires requires(std::string& out, T& in) { toChars(out, in); }
inline constexpr std::string toString(const T& x) {
    std::string out;
    toChars(out, x);
    return out;
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
