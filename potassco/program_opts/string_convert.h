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

#include <potassco/enum.h>

#include <cassert>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace Potassco {
namespace Detail {
std::from_chars_result parseChar(std::string_view in, char& out);
std::from_chars_result parseUnsigned(std::string_view in, std::uintmax_t& out, std::uintmax_t max);
std::from_chars_result parseSigned(std::string_view in, std::intmax_t& out, std::intmax_t min, std::intmax_t max);
std::from_chars_result parseFloat(std::string_view in, double& out, double min, double max);
} // namespace Detail
namespace Parse {
template <typename T>
requires(std::is_same_v<T, std::errc> || std::is_convertible_v<T, std::from_chars_result>)
constexpr auto ok(T ec) {
    if constexpr (std::is_same_v<T, std::errc>) {
        return ec == std::errc{};
    }
    else {
        return ec.ec == std::errc{};
    }
}
constexpr std::from_chars_result error(std::string_view& x, std::errc ec = std::errc::invalid_argument) {
    return {.ptr = std::data(x), .ec = ec};
}
constexpr std::from_chars_result success(std::string_view& x, std::size_t pop) {
    assert(pop <= x.length());
    x.remove_prefix(pop);
    return {.ptr = std::data(x), .ec = {}};
}
bool matchOpt(std::string_view& in, char v);
bool eqIgnoreCase(std::string_view, std::string_view);
bool eqIgnoreCase(std::string_view, std::string_view, std::size_t n);
} // namespace Parse
///////////////////////////////////////////////////////////////////////////////
// chars -> T
///////////////////////////////////////////////////////////////////////////////
template <typename T>
constexpr std::errc    extract(std::string_view& in, T& out);
std::from_chars_result fromChars(std::string_view in, bool& out);
template <std::integral T>
requires(not std::is_same_v<T, bool>)
std::from_chars_result fromChars(std::string_view in, T& out) {
    std::from_chars_result res; // NOLINT
    if constexpr (std::is_unsigned_v<T>) {
        std::uintmax_t temp;
        if (res = Detail::parseUnsigned(in, temp, std::numeric_limits<T>::max()); Parse::ok(res)) {
            out = static_cast<T>(temp);
        }
    }
    else {
        std::intmax_t temp;
        if (res = Detail::parseSigned(in, temp, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max());
            Parse::ok(res)) {
            out = static_cast<T>(temp);
        }
    }
    if constexpr (std::is_same_v<T, char>) {
        if (not Parse::ok(res)) {
            char temp;
            if (res = Detail::parseChar(in, temp); Parse::ok(res)) {
                out = temp;
            }
        }
    }
    return res;
}
template <std::floating_point T>
std::from_chars_result fromChars(std::string_view in, T& out) {
    double temp;
    auto   r = Detail::parseFloat(in, temp, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max());
    if (Parse::ok(r)) {
        out = static_cast<T>(temp);
    }
    return r;
}
inline std::from_chars_result fromChars(std::string_view in, std::string& out) {
    out.append(in);
    return Parse::success(in, in.size());
}
inline std::from_chars_result fromChars(std::string_view in, std::string_view& out) {
    out = in;
    return Parse::success(in, in.size());
}

// Parses T[,U] optionally enclosed in parentheses.
// TODO: Why do we allow single values? This should be ok only if U is std::optional.
template <typename T, typename U>
std::from_chars_result fromChars(std::string_view in, std::pair<T, U>& out) {
    auto temp(out);
    bool m = Parse::matchOpt(in, '(');
    if (auto r = extract(in, temp.first); not Parse::ok(r)) {
        return Parse::error(in, r);
    }
    if (std::errc r; Parse::matchOpt(in, ',') && not Parse::ok(r = extract(in, temp.second))) {
        return Parse::error(in, r);
    }
    if (m && not Parse::matchOpt(in, ')')) {
        return Parse::error(in);
    }
    out = std::move(temp);
    return Parse::success(in, 0);
}

// parses T1 [, ..., Tn] optionally enclosed in brackets
template <typename C>
requires requires(C c, std::string_view in) {
    typename C::value_type;
    c.push_back(std::declval<typename C::value_type>());
}
std::from_chars_result fromChars(std::string_view in, C& out) {
    auto m = Parse::matchOpt(in, '[');
    for (typename C::value_type temp; not in.empty();) {
        if (auto r = extract(in, temp); not Parse::ok(r)) {
            return Parse::error(in, r);
        }
        out.push_back(std::move(temp));
        if (in.size() < 2 || not Parse::matchOpt(in, ',')) {
            break;
        }
    }
    if (m && not Parse::matchOpt(in, ']')) {
        return Parse::error(in);
    }
    return Parse::success(in, 0);
}

template <HasEnumEntries EnumT>
std::from_chars_result fromChars(std::string_view in, EnumT& out) {
    // try numeric extraction first
    using U = std::underlying_type_t<EnumT>;
    using T = std::conditional_t<not std::is_same_v<U, char>, U, int>;
    T    v;
    auto ret = fromChars(in, v);
    if (Parse::ok(ret)) {
        if (enum_cast<EnumT>(v).has_value()) {
            out = static_cast<EnumT>(v);
        }
        else {
            ret = Parse::error(in);
        }
    }
    else {
        // try extraction "by name"
        for (const auto& [key, val] : Potassco::enum_entries<EnumT>()) {
            auto n = val.size();
            if (Parse::eqIgnoreCase(in, val, n) && (n == in.size() || in[n] == ',')) {
                out = static_cast<EnumT>(key);
                ret = Parse::success(in, n);
                break;
            }
        }
    }
    return ret;
}

template <typename T>
constexpr std::errc extract(std::string_view& in, T& out) {
    auto r = fromChars(in, out);
    if (Parse::ok(r)) {
        auto dist = static_cast<std::size_t>(r.ptr - in.data());
        assert(dist <= in.size());
        in.remove_prefix(dist);
        return {};
    }
    return r.ec;
}
///////////////////////////////////////////////////////////////////////////////
// string -> T
///////////////////////////////////////////////////////////////////////////////
template <typename T>
std::errc stringTo(std::string_view arg, T& x) {
    if (auto r = fromChars(arg, x); not Parse::ok(r)) {
        return r.ec;
    }
    else {
        return r.ptr == arg.data() + arg.size() ? std::errc{} : std::errc::invalid_argument;
    }
}

} // namespace Potassco
