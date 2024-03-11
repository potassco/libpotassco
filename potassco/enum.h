//
// Copyright (c) 2024 - present, Benjamin Kaufmann
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

#include <array>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Potassco {
namespace detail {
template <typename EnumT>
struct EnumHelper {
    using UT = std::underlying_type_t<EnumT>;

    constexpr EnumHelper() = default;
    constexpr EnumHelper(UT u) : v(u) {}

    template <typename T>
    constexpr EnumHelper operator=(T t) const noexcept { // NOLINT
        return EnumHelper(static_cast<UT>(t));
    }

    [[maybe_unused]] static void consteval_failure(const char*) {}

    static consteval auto popName(std::string_view& nameView) {
        auto next = nameView.find_first_of(" ,=");
        auto ret  = nameView.substr(0, next);
        if (next < nameView.size()) {
            nameView.remove_prefix(next);
            next = nameView.find_first_of(",<(");
            if (next < nameView.size()) {
                if (nameView[next] != ',') {
                    consteval_failure("Unsupported enum type: enumerator init expression too complex");
                }
                next = nameView.find_first_not_of(' ', next + 1);
            }
        }
        nameView.remove_prefix(next <= nameView.size() ? next : nameView.size());
        return ret;
    }

    template <size_t N>
    static consteval auto makeEntries(const EnumHelper<EnumT> (&values)[N], std::string_view nameView) {
        std::array<std::pair<EnumT, std::string_view>, N> ret{};
        UT                                                prev = 0;
        for (size_t i = 0; i != N; ++i) {
            UT curr = values[i].v ? *values[i].v : prev + (i > 0);
            if (i > 0 && curr < prev) {
                consteval_failure("Unsupported enum type: enumerators must be given in increasing order");
            }
            ret[i].first  = static_cast<EnumT>(curr);
            ret[i].second = popName(nameView);
            prev          = curr;
        }
        return ret;
    }

    std::optional<UT> v;
};
} // namespace detail

template <typename T>
using enum_type = std::type_identity<T>;

template <typename EnumT>
consteval auto enumDecl(EnumT v, std::string_view n) {
    return std::pair{v, n};
}

template <typename T>
inline constexpr auto c_type = std::type_identity<T>{};

template <typename T, typename = void>
struct EnumReflect : std::false_type {};

template <typename EnumT>
struct EnumReflect<EnumT, std::void_t<decltype(getEnumEntries(c_type<EnumT>))>> : std::true_type {
    using underlying_type           = std::underlying_type_t<EnumT>;
    static constexpr auto c_entries = getEnumEntries(c_type<EnumT>);
};

#define POTASSCO_ENUM_IMPL(ENUM_T, BASE_T, FRIEND, ...)                                                                \
    enum class ENUM_T : BASE_T { __VA_ARGS__ };                                                                        \
    FRIEND consteval auto getEnumEntries(std::type_identity<ENUM_T>) {                                                 \
        constexpr Potassco::detail::EnumHelper<ENUM_T> __VA_ARGS__;                                                    \
        constexpr Potassco::detail::EnumHelper<ENUM_T> arr[] = {__VA_ARGS__};                                          \
        return Potassco::detail::EnumHelper<ENUM_T>::makeEntries(arr, #__VA_ARGS__);                                   \
    }                                                                                                                  \
    static_assert(Potassco::enum_count<ENUM_T>() > 0, "Potassco enum must not be empty")

//! Defines a scoped enum at namespace scope with primitive reflection support.
/*!
 *
 * \param ENUM_T The name of the enum to be defined. \
 * \param BASE_T The underlying (numeric) type of the enum. \
 * \param  ...   The individual enumerators. \
 *
 * Example:
 * POTASSCO_ENUM(Color, unsigned, red = 0, green, blue);
 *
 * Reflection support:
 * - enum_count<EnumT>()   : Returns the number of enumerators in the enum, e.g. 3 for example enum Color.
 * - enum_name(EnumT e)    : Returns the name of the given element e as std::string_view, e.g. "red" for Color:red.
 * - enum_cast<EnumT>(n)   : Returns the enumerator with the given value n as an optional, e.g. Color::red for 0.
 * - enum_entries<EnumT>() : Returns the enumerators of the enum as an array of (name, "name")-pairs, e.g.
 *                           [(Color::red, "red"), (Color::green, "green"), (Color::blue, "blue")] for enum Color.
 */
#define POTASSCO_ENUM(ENUM_T, BASE_T, ...) POTASSCO_ENUM_IMPL(ENUM_T, BASE_T, , __VA_ARGS__)

//! Defines a scoped enum at class scope with primitive reflection support.
/*!
 * Example:
 * struct Foo {
 *  POTASSCO_NESTED_ENUM(Color, unsigned, red = 0, green, blue);
 * };
 */
#define POTASSCO_NESTED_ENUM(ENUM_T, BASE_T, ...) POTASSCO_ENUM_IMPL(ENUM_T, BASE_T, friend, __VA_ARGS__)

//! Returns the elements of the given enum as an array of (name, "name")-pairs.
/*!
 * \tparam EnumT An enum type with reflection support.
 */
template <typename EnumT>
requires EnumReflect<EnumT>::value
consteval auto enum_entries() {
    return EnumReflect<EnumT>::c_entries;
}

//! Returns the number of enumerators in the given enum type.
/*!
 * \tparam EnumT An enum type with reflection support.
 * \return Number of enumerators.
 */
template <typename EnumT>
requires EnumReflect<EnumT>::value
consteval auto enum_count() -> std::size_t {
    return std::size(EnumReflect<EnumT>::c_entries);
}

//! Returns the name of the given enumerator e.
/*!
 * \tparam EnumT An enum type with reflection support.
 * \param e Enumerator for which the name should be returned.
 * \return The stringified name of e or an empty string_view if e is not a named enumerator of EnumT.
 */
template <typename EnumT>
requires EnumReflect<EnumT>::value
constexpr auto enum_name(EnumT e) -> std::string_view {
    for (const auto& kv : EnumReflect<EnumT>::c_entries) {
        if (kv.first == e) {
            return kv.second;
        }
    }
    return std::string_view{};
}

template <typename EnumT>
requires EnumReflect<EnumT>::value
constexpr auto enum_cast(std::underlying_type_t<EnumT> n) -> std::optional<EnumT> {
    using UT = typename EnumReflect<EnumT>::underlying_type;
    return not EnumReflect<EnumT>::c_entries.empty() &&
                   n >= static_cast<UT>(EnumReflect<EnumT>::c_entries.front().first) &&
                   n <= static_cast<UT>(EnumReflect<EnumT>::c_entries.back().first)
               ? std::optional{static_cast<EnumT>(n)}
               : std::optional<EnumT>{};
}

} // namespace Potassco
