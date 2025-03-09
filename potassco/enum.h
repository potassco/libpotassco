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

#include <potassco/platform.h>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>
namespace Potassco {
#if defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying == 202102L
using std::to_underlying;
#else
template <typename EnumT>
requires(std::is_enum_v<EnumT>)
[[nodiscard]] constexpr std::underlying_type_t<EnumT> to_underlying(EnumT e) noexcept {
    return static_cast<std::underlying_type_t<EnumT>>(e);
}
#endif
namespace Detail {
// NOLINTBEGIN
template <typename T>
inline constexpr auto c_type = std::type_identity<T>{};

template <typename T, typename = void>
struct EnumMeta : std::false_type {
    static constexpr auto min() {
        if constexpr (requires { enumMin(c_type<T>); }) {
            return enumMin(c_type<T>);
        }
        else {
            return std::numeric_limits<std::underlying_type_t<T>>::min();
        }
    }
    static constexpr auto max() {
        if constexpr (requires { enumMax(c_type<T>); }) {
            return enumMax(c_type<T>);
        }
        else {
            return std::numeric_limits<std::underlying_type_t<T>>::max();
        }
    }
};

template <typename EnumT>
struct EnumMeta<EnumT, std::void_t<decltype(enable_meta(c_type<EnumT>))>> : std::true_type {
    using underlying_type              = std::underlying_type_t<EnumT>;
    static constexpr auto c_meta       = enable_meta(c_type<EnumT>);
    static constexpr auto meta_entries = requires { c_meta.entries(); };
    static constexpr auto c_verify     = []() -> bool {
        if constexpr (meta_entries) {
            static_assert(std::adjacent_find(c_meta.entries().begin(), c_meta.entries().end(),
                                                 [](const auto& lhs, const auto& rhs) {
                                                 return to_underlying(lhs.first) == to_underlying(rhs.first);
                                             }) == c_meta.entries().end(),
                          "enumerators must have unique values!");
        }
        return true;
    }();

    static constexpr underlying_type  min() { return c_meta.min(); }
    static constexpr underlying_type  max() { return c_meta.max(); }
    static constexpr std::size_t      count() { return c_meta.count(); }
    static constexpr bool             valid(underlying_type v) { return c_meta.valid(v); }
    static constexpr std::string_view name(EnumT e) { return c_meta.name(e); }
};

template <std::size_t N>
struct FixedString {
    constexpr FixedString(std::string_view s) {
        for (std::size_t i = 0; i != N; ++i) { data[i] = s[i]; }
        data[N] = 0;
    }
    [[nodiscard]] constexpr auto operator<=>(const FixedString&) const = default;
    [[nodiscard]] constexpr      operator std::string_view() const { return {data, N}; }
    [[nodiscard]] constexpr auto size() const { return N; }
    char                         data[N + 1];
};
template <auto S>
consteval const auto& data() {
    return S.data;
}
template <auto V>
[[nodiscard]] constexpr auto fn() noexcept -> std::string_view {
    return std::source_location::current().function_name();
}

enum class _enum { VALUE };
struct EnumMetaBase {
    static constexpr auto needle = std::string_view{"Potassco::Detail::_enum::VALUE"};
    static constexpr auto name   = fn<_enum::VALUE>();
    static constexpr auto start  = name.find(needle);
    static constexpr auto rest   = std::size(name) - (start + needle.size());
};
template <typename EnumT, EnumT V>
requires std::is_enum_v<EnumT>
[[nodiscard]] constexpr auto enumName() {
    constexpr auto func = fn<V>();
    constexpr auto name = func.substr(EnumMetaBase::start, func.size() - EnumMetaBase::rest - EnumMetaBase::start);
    constexpr auto dcl  = name.starts_with('(') ? name : name.substr(name.find_last_of("::") + 1);
    static_assert(not dcl.empty());
    return data<FixedString<dcl.size()>{dcl.data()}>();
}
static_assert(enumName<_enum, _enum::VALUE>() == std::string_view("VALUE"));
template <typename EnumT, EnumT V>
requires std::is_enum_v<EnumT>
constexpr void addEntry(std::pair<EnumT, std::string_view>*& out) {
    if constexpr (*enumName<EnumT, V>() != '(') {
        *out++ = {V, enumName<EnumT, V>()};
    }
}
template <typename EnumT, typename... Args>
constexpr void addEntries(std::pair<EnumT, std::string_view>* out, EnumT e, std::string_view n, Args... args) {
    *out++ = {e, n};
    if constexpr (sizeof...(args) != 0) {
        addEntries(out, args...);
    }
}
template <typename Op, typename E, typename... Args>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr E applyBitOp(Op op, E arg1, Args... args) noexcept {
#if defined(__clang__)
    [[clang::suppress]] // suppress clang analyzer warning optin.core.EnumCastOutOfRange
#endif
    return static_cast<E>(op(Potassco::to_underlying(arg1), Potassco::to_underlying(args)...));
}
// NOLINTEND
} // namespace Detail

/*!
 * \addtogroup BasicTypes
 */
///@{

//! Meta type for simple (consecutive) enums with @c Count elements starting at @c First.
/*!
 * To associate default metadata for some enum Foo defined in namespace X, either define a consteval function
 * <tt>auto enable_meta(std::type_identity<Foo>) { return Potassco::DefaultEnum{...}; }</tt> or call one of the
 * POTASSCO_SET_DEFAULT_ENUM_ macros in namespace X.
 */
template <typename EnumT, std::size_t Count, std::underlying_type_t<EnumT> First = 0>
struct DefaultEnum {
    using UT = std::underlying_type_t<EnumT>; // NOLINT
    [[nodiscard]] static constexpr auto min() { return First; }
    [[nodiscard]] static constexpr auto max() { return static_cast<UT>(count() - (count() != 0)) + min(); }
    [[nodiscard]] static constexpr auto count() { return Count; }
    [[nodiscard]] static constexpr bool valid(UT v) { return v >= min() && v <= max(); }
};

//! Convenience macro for specifying metadata for consecutive and zero-based enums.
/*!
 * \param MAX_E The last (maximal) enumerator of the enum type for which metadata is specified.
 */
#define POTASSCO_SET_DEFAULT_ENUM_MAX(MAX_E)                                                                           \
    POTASSCO_SET_DEFAULT_ENUM_COUNT(decltype(MAX_E), (1u + Potassco::to_underlying(MAX_E)))

//! Convenience macro for specifying metadata for consecutive enums.
/*!
 * \param E     The enum type for which metadata is specified.
 * \param COUNT Number of enumerators in enum E.
 * \param ...   Value of first (minimal) enumerator or 0 if not given.
 */
#define POTASSCO_SET_DEFAULT_ENUM_COUNT(E, COUNT, ...)                                                                 \
    [[maybe_unused]] consteval auto enable_meta(std::type_identity<E>) {                                               \
        return Potassco::DefaultEnum<E, (COUNT) POTASSCO_OPTARGS(__VA_ARGS__)>();                                      \
    }                                                                                                                  \
    static_assert(Potassco::enum_count<E>() == (COUNT))

//! Convenience macro for specifying metadata for consecutive enums.
/*!
 * \param MIN_E The first (minimal) enumerator of the enum type for which metadata is specified.
 * \param MAX_E The last (maximal) enumerator of the enum type for which metadata is specified.
 */
#define POTASSCO_SET_DEFAULT_ENUM_RANGE(MIN_E, MAX_E)                                                                  \
    POTASSCO_SET_DEFAULT_ENUM_COUNT(decltype(MIN_E),                                                                   \
                                    (1u + (Potassco::to_underlying(MAX_E) - Potassco::to_underlying(MIN_E))),          \
                                    Potassco::to_underlying(MIN_E))

//! Meta type for enums with @c N explicit elements, where each element is an enumerator and its (stringified) name.
/*!
 * \note Enumerators shall have unique numeric values.
 */
template <typename EnumT, std::size_t N>
struct EnumEntries {
    using element_type              = std::pair<EnumT, std::string_view>; // NOLINT
    using container_type            = std::array<element_type, N>;        // NOLINT
    using UT                        = std::underlying_type_t<EnumT>;      // NOLINT
    static constexpr auto null_elem = element_type{};
    explicit constexpr EnumEntries(const element_type* base) {
        for (std::size_t i = 0; i != N; ++i) { vals[i] = base[i]; }
        std::sort(vals.begin(), vals.end(),
                  [](const auto& lhs, const auto& rhs) { return to_underlying(lhs.first) < to_underlying(rhs.first); });
    }
    [[nodiscard]] constexpr auto min() const noexcept { return to_underlying(vals[0].first); }
    [[nodiscard]] constexpr auto max() const noexcept { return to_underlying(vals[N - 1].first); }
    [[nodiscard]] constexpr auto count() const noexcept { return N; } /* NOLINT */
    [[nodiscard]] constexpr auto trivial() const noexcept {
        return min() == 0 && static_cast<std::size_t>(max()) == count() - 1;
    }
    [[nodiscard]] constexpr bool valid(UT v) const noexcept { return find(static_cast<EnumT>(v)) != &null_elem; }
    [[nodiscard]] constexpr std::string_view    name(EnumT e) const noexcept { return find(e)->second; }
    [[nodiscard]] constexpr const element_type* find(EnumT e) const noexcept {
        if (trivial()) {
            return to_underlying(e) >= min() && to_underlying(e) <= max() ? vals.data() + to_underlying(e) : &null_elem;
        }
        auto it = std::lower_bound(vals.begin(), vals.end(), e, [](const auto& lhs, EnumT rhs) {
            return to_underlying(lhs.first) < to_underlying(rhs);
        });
        return it != vals.end() && it->first == e ? &*it : &null_elem;
    }
    constexpr const container_type& entries() const noexcept { return vals; }

    container_type vals{};
};

//! Returns enum entries from the given arguments.
template <typename EnumT, typename... Args>
consteval auto makeEntries(EnumT e1, std::string_view n1,
                           Args... args) -> EnumEntries<EnumT, (sizeof...(Args) + 2) / 2> {
    constexpr auto                     N = (sizeof...(Args) + 2) / 2;
    std::pair<EnumT, std::string_view> r[N];
    Detail::addEntries(r, e1, n1, args...);
    return EnumEntries<EnumT, N>(r);
}

//! Convenience macro for specifying metadata for enums with explicitly named entries.
/*!
 * \param E   The enum type for which metadata is specified.
 * \param ... List of entries given as {<dcl>, "<name>"} pairs, where <dcl> is an enumerator of E and "<name>" its
 *            name.
 */
#define POTASSCO_SET_ENUM_ENTRIES(E, ...)                                                                              \
    [[maybe_unused]] consteval auto enable_meta(std::type_identity<E>) {                                               \
        using enum E;                                                                                                  \
        using namespace std::literals;                                                                                 \
        constexpr std::pair<E, std::string_view> e[] = {__VA_ARGS__};                                                  \
        return Potassco::EnumEntries<E, std::size(e)>(e);                                                              \
    }                                                                                                                  \
    static_assert(Potassco::HasEnumEntries<E>)

//! Returns valid enum entries of the given enum in the range [Min, Max].
template <typename EnumT, auto Min = 0, auto Max = 128>
requires(std::is_enum_v<EnumT> && Max >= Min)
consteval auto reflectEntries() {
    constexpr auto all = []<auto... Is>(std::index_sequence<Is...>) {
        std::array<std::pair<EnumT, std::string_view>, sizeof...(Is)> r;
        auto*                                                         p = r.data();
        (Detail::addEntry<EnumT, static_cast<EnumT>(static_cast<std::underlying_type_t<EnumT>>(Is) + Min)>(p), ...);
        return std::pair{r, static_cast<std::size_t>(p - r.data())};
    }(std::make_index_sequence<(Max - Min) + 1>());
    return EnumEntries<EnumT, all.second>{all.first.data()};
}

//! Convenience macro for specifying metadata for enums with reflected names.
/*!
 * \param E   The enum type for which metadata is specified.
 * \param ... Optional numeric reflection range (default is [0;128])
 */
#define POTASSCO_REFLECT_ENUM_ENTRIES(E, ...)                                                                          \
    [[maybe_unused]] consteval auto enable_meta(std::type_identity<E>) {                                               \
        return Potassco::reflectEntries<E, __VA_ARGS__>();                                                             \
    }                                                                                                                  \
    static_assert(Potassco::HasEnumEntries<E>)

//! Concept for scoped enums.
template <typename EnumT>
concept ScopedEnum = std::is_enum_v<EnumT> && not std::is_convertible_v<EnumT, std::underlying_type_t<EnumT>>;

//! Concept for enums with metadata.
template <typename EnumT>
concept HasEnumMeta = std::is_enum_v<EnumT> && Detail::EnumMeta<EnumT>::value;

//! Concept for enums whose metadata includes individual entries.
template <typename EnumT>
concept HasEnumEntries = HasEnumMeta<EnumT> && Detail::EnumMeta<EnumT>::meta_entries;

//! Returns the elements of the given enum as an array of (name, "name")-pairs.
/*!
 * \tparam EnumT An enum type with full metadata.
 */
template <HasEnumEntries EnumT>
consteval decltype(auto) enum_entries() {
    return Detail::EnumMeta<EnumT>::c_meta.entries();
}

//! Returns the number of enumerators in the given enum type.
/*!
 * \tparam EnumT An enum type with metadata.
 * \return Number of enumerators.
 */
template <HasEnumMeta EnumT>
consteval auto enum_count() -> std::size_t {
    return Detail::EnumMeta<EnumT>::count();
}

//! Returns the minimal valid numeric value for the given enum type.
/*!
 * If EnumT has associated metadata, the minimal value is determined based on that data.
 * Otherwise, the minimal value is the minimal value of the enum's underlying type.
 */
template <typename EnumT>
requires(std::is_enum_v<EnumT>)
constexpr auto enum_min() -> std::underlying_type_t<EnumT> {
    return Detail::EnumMeta<EnumT>::min();
}

//! Returns the maximal valid numeric value for the given enum type.
/*!
 * If EnumT has associated metadata, the maximal value is determined based on that data.
 * Otherwise, the maximal value is the maximal value of the enum's underlying type.
 */
template <typename EnumT>
requires(std::is_enum_v<EnumT>)
constexpr auto enum_max() -> std::underlying_type_t<EnumT> {
    return Detail::EnumMeta<EnumT>::max();
}

//! Returns the name of the given enumerator @c e.
/*!
 * \tparam EnumT An enum type with full metadata.
 * \param e Enumerator for which the name should be returned.
 * \return The stringified name of @c e or an empty string_view if @c e is not a named enumerator of EnumT.
 */
template <HasEnumEntries EnumT>
constexpr auto enum_name(EnumT e) -> std::string_view {
    return Detail::EnumMeta<EnumT>().name(e);
}

//! Tries to convert the given integral value into an enumerator of EnumT.
/*!
 * \tparam EnumT An enum type with metadata.
 * \return An enumerator of EnumT with integral value @n or an empty optional if no such enumerator exists in EnumT.
 */
template <HasEnumMeta EnumT>
constexpr auto enum_cast(std::underlying_type_t<EnumT> n) -> std::optional<EnumT> {
    return Detail::EnumMeta<EnumT>::valid(n) ? std::optional{static_cast<EnumT>(n)} : std::optional<EnumT>{};
}

//! Returns whether @c x is a superset of @c y.
template <typename T>
requires(std::is_enum_v<T>)
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test(T x, T y) noexcept {
    return (to_underlying(x) & to_underlying(y)) == to_underlying(y);
}
///@}

//! Opt-in macro for enabling heterogeneous comparison operators for a given scoped enum type.
/*!
 * A scoped enum E for which POTASSCO_ENABLE_CMP_OPS(E) has been called can be implicitly compared to its underlying
 * type. Furthermore, the unary operator+ can be used to convert members of E to their underlying numeric value.
 *
 * \note If E is a class-local enum, POTASSCO_ENABLE_CMP_OPS(E, friend) can be used to enable comparison operators
 *       from within the class definition.
 */
#define POTASSCO_ENABLE_CMP_OPS(E, ...)                                                                                \
    [[nodiscard]] POTASSCO_E_OP(==, (E lhs, std::underlying_type_t<E> rhs), __VA_ARGS__)->bool {                       \
        return Potassco::to_underlying(lhs) == rhs;                                                                    \
    }                                                                                                                  \
    [[nodiscard]] POTASSCO_E_OP(<=>, (E lhs, std::underlying_type_t<E> rhs), __VA_ARGS__) {                            \
        return Potassco::to_underlying(lhs) <=> rhs;                                                                   \
    }                                                                                                                  \
    [[nodiscard]] POTASSCO_E_OP(+, (E v), __VA_ARGS__)->std::underlying_type_t<E> {                                    \
        return Potassco::to_underlying(v);                                                                             \
    }                                                                                                                  \
    static_assert(Potassco::ScopedEnum<E>)

//! Opt-in macro for enabling bit operations for a given enum type.
/*!
 * Use POTASSCO_ENABLE_BIT_OPS(E) to enable bitwise operators on the underlying type of enum E.
 *
 * \note If E is a class-local enum, POTASSCO_ENABLE_BIT_OPS(E, friend) can be used to enable bitwise operators from
 *       within the class definition.
 */
#define POTASSCO_ENABLE_BIT_OPS(E, ...)                                                                                \
    [[nodiscard]] POTASSCO_E_OP(~, (E a), __VA_ARGS__)->E { return Potassco::Detail::applyBitOp(std::bit_not{}, a); }  \
    [[nodiscard]] POTASSCO_E_OP(|, (E a, E b), __VA_ARGS__)->E {                                                       \
        return Potassco::Detail::applyBitOp(std::bit_or{}, a, b);                                                      \
    }                                                                                                                  \
    POTASSCO_E_OP(|=, (E & a, E b), __VA_ARGS__)->E& { return a = a | b; }                                             \
    [[nodiscard]] POTASSCO_E_OP(&, (E a, E b), __VA_ARGS__)->E {                                                       \
        return Potassco::Detail::applyBitOp(std::bit_and{}, a, b);                                                     \
    }                                                                                                                  \
    POTASSCO_E_OP(&=, (E & a, E b), __VA_ARGS__)->E& { return a = a & b; }                                             \
    [[nodiscard]] POTASSCO_E_OP(^, (E a, E b), __VA_ARGS__)->E {                                                       \
        return Potassco::Detail::applyBitOp(std::bit_xor{}, a, b);                                                     \
    }                                                                                                                  \
    POTASSCO_E_OP(^=, (E & a, E b), __VA_ARGS__)->E& { return a = a ^ b; }                                             \
    static_assert(std::is_enum_v<E>)

#define POTASSCO_E_OP(op, arg, ...)                                                                                    \
    [[maybe_unused]] POTASSCO_FORCE_INLINE __VA_ARGS__ constexpr auto operator op arg noexcept

} // namespace Potassco
