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

#include <bit>
#include <climits>
#include <concepts>
#if !defined(__cpp_lib_int_pow2) || __cpp_lib_int_pow2 < 202002L
#error "unsupported compiler"
#endif

namespace Potassco {
/*!
 * \addtogroup BasicTypes
 */
///@{
using std::bit_ceil;
using std::bit_floor;
using std::bit_width;
using std::countl_one;
using std::countl_zero;
using std::countr_one;
using std::countr_zero;
using std::has_single_bit;
using std::popcount;
using std::rotl;
using std::rotr;

/*!
 * Functions on individual bits
 */
///@{
//! Returns a value of T with bit `n` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T nth_bit(std::common_type_t<unsigned, T> n) {
    if (not std::is_constant_evaluated()) {
        POTASSCO_DEBUG_ASSERT(n < (sizeof(T) * CHAR_BIT));
    }
    return static_cast<T>(1) << n;
}
static_assert(nth_bit<unsigned>(0) == 1u && nth_bit<unsigned>(3) == 0b00001000u);
//! Returns whether bit `n` is set in `x`.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_bit(T x, std::common_type_t<unsigned, T> n) noexcept {
    return (x & nth_bit<T>(n)) != 0;
}
static_assert(test_bit(7u, 0) && not test_bit(8u, 4) && test_bit(nth_bit<unsigned>(2), 2));
//! Returns a copy of `x` with bit `n` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T set_bit(T x, std::common_type_t<unsigned, T> n) noexcept {
    return x | nth_bit<T>(n);
}
static_assert(set_bit(6u, 0) == 7u && set_bit(8u, 1) == 10u);
//! Effect: x = set_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_set_bit(T& x, std::common_type_t<unsigned, T> n) noexcept {
    return x |= nth_bit<T>(n);
}
//! Returns a copy of `x` with bit `n` cleared.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T clear_bit(T x, std::common_type_t<unsigned, T> n) noexcept {
    return x & static_cast<T>(~nth_bit<T>(n));
}
static_assert(clear_bit(7u, 0) == 6u && clear_bit(8u, 3) == 0u);
//! Effect: x = clear_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_clear_bit(T& x, std::common_type_t<unsigned, T> n) noexcept {
    return x &= static_cast<T>(~nth_bit<T>(n));
}
//! Returns a copy of `x` with bit `n` toggled.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T toggle_bit(T x, std::common_type_t<unsigned, T> n) noexcept {
    return x ^ nth_bit<T>(n);
}
static_assert(toggle_bit(6u, 0) == 7u && toggle_bit(7u, 1) == 5u);
//! Effect: x = toggle_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_toggle_bit(T& x, std::common_type_t<unsigned, T> n) noexcept {
    return x ^= nth_bit<T>(n);
}
///@}
/*!
 * Functions on bit masks
 */
///@{
//! Returns whether `x` has all set bits in the mask `m` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_mask(T x, std::type_identity_t<T> m) noexcept {
    return (x & m) == m;
}
static_assert(test_mask(15u, 7u) && not test_mask(10u, 6u));
//! Returns whether `x` has any set bits in the mask `m` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_any(T x, std::type_identity_t<T> m) noexcept {
    return (x & m) != 0;
}
static_assert(test_any(15u, 7u) && test_any(10u, 6u));
//! Returns a copy of `x` with all set bits in the mask `m` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T set_mask(T x, std::type_identity_t<T> m) noexcept {
    return x | m;
}
static_assert(set_mask(6u, 3u) == 7u && set_mask(1024u, 7u) == 1031u);
//! Effect: x = set_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_set_mask(T& x, std::type_identity_t<T> m) noexcept {
    return x |= m;
}
//! Returns a copy of `x` with all set bits in the mask `m` cleared.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T clear_mask(T x, std::type_identity_t<T> m) noexcept {
    return x & static_cast<T>(~m);
}
static_assert(clear_mask(7u, 3u) == 4u && clear_mask(19u, 17u) == 2u);
//! Effect: x = clear_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_clear_mask(T& x, std::type_identity_t<T> m) noexcept {
    return x &= static_cast<T>(~m);
}
//! Returns a copy of `x` with all bits in the mask `m` toggled.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T toggle_mask(T x, std::type_identity_t<T> m) noexcept {
    return x ^ m;
}
//! Effect: x = toggle_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_toggle_mask(T& x, std::type_identity_t<T> m) noexcept {
    return x ^= m;
}
///@}
//! Returns a value of T with the first `numBits` set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T bit_max(std::common_type_t<unsigned, T> numBits) {
    return numBits < (sizeof(T) * CHAR_BIT) ? nth_bit<T>(numBits) - 1 : ~static_cast<T>(0);
}
static_assert(bit_max<unsigned>(0) == 0u && bit_max<unsigned>(3) == 7u && bit_max<uint32_t>(32) == UINT32_MAX);

//! Returns a copy of `x` with only the right most set bit set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T right_most_bit(T x) noexcept {
    POTASSCO_WARNING_PUSH()
    POTASSCO_WARNING_IGNORE_MSVC(4146) // unary minus operator applied to unsigned type, result still unsigned
    return x & -x;
    POTASSCO_WARNING_POP()
}
static_assert(right_most_bit(0b00000000u) == 0b00000000u && right_most_bit(0b00010100u) == 0b00000100u);
//! Returns a copy of `x` with only the left most set bit set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T left_most_bit(T x) noexcept {
    return std::bit_floor(x);
}
static_assert(left_most_bit(0b00000000u) == 0b00000000u && left_most_bit(0b00010100u) == 0b00010000u);
//! Returns the log2 of `x`.
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr unsigned log2(T x) noexcept {
    return static_cast<unsigned>(std::bit_width(x)) - static_cast<unsigned>(x != 0u);
}
static_assert(log2(0u) == 0u && log2(1u) == 0u && log2(2u) == 1u && log2(4u) == 2u && log2(255u) == 7u);
//! Returns the number of set bits in the value of x.
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr unsigned bit_count(T x) noexcept {
    return static_cast<unsigned>(std::popcount(x));
}
static_assert(bit_count(0u) == 0u && bit_count(1u) == 1u && bit_count(127u) == 7u);

template <std::unsigned_integral T, typename ElemType = unsigned>
requires requires(ElemType e) {
    { +e } -> std::convertible_to<unsigned>;
}
class Bitset {
public:
    using StorageType = T;
    //! Maximal number of elements in the set (i.e., maximal number of bits)
    static constexpr auto max_count = sizeof(T) * CHAR_BIT;

    //! Creates an empty set, i.e., all bits are zero.
    constexpr Bitset() noexcept = default;
    //! Creates a set with the given elements, i.e., bits at the given positions are set.
    constexpr Bitset(std::initializer_list<ElemType> elems) {
        for (StorageType zero{}; auto e : elems) { set_ |= Potassco::set_bit(zero, bit(e)); }
    }
    //! Constructs a bitset with all bits in `r` set.
    static constexpr Bitset fromRep(StorageType r) noexcept { return Bitset(r); }
    //! Returns whether the set contains the given element.
    [[nodiscard]] constexpr bool contains(ElemType e) const { return Potassco::test_bit(set_, bit(e)); }
    //! Returns the number of elements in the set, i.e., the number of bits set.
    [[nodiscard]] constexpr unsigned count() const noexcept { return Potassco::bit_count(set_); }
    //! Adds the given element to the set and returns true if it was not already in the set.
    constexpr bool add(ElemType e) { return not contains(e) && Potassco::store_set_bit(set_, bit(e)); }
    //! Removes the given element from the set and returns true if it was in the set.
    constexpr bool remove(ElemType e) { return contains(e) && Potassco::store_clear_bit(set_, bit(e)) >= 0u; }
    //! Removes all elements (bits) >= max.
    constexpr void removeMax(ElemType max) { set_ &= Potassco::bit_max<StorageType>(+max); }
    //! Removes all elements from the set.
    constexpr void clear() noexcept { set_ = {}; }

    [[nodiscard]] constexpr StorageType rep() const noexcept { return set_; }

    friend constexpr bool operator==(Bitset lhs, Bitset rhs) noexcept  = default;
    friend constexpr auto operator<=>(Bitset lhs, Bitset rhs) noexcept = default;

private:
    POTASSCO_FORCE_INLINE static constexpr unsigned bit(ElemType e) { return static_cast<unsigned>(+e); }
    constexpr explicit Bitset(StorageType r) : set_(r) {}
    StorageType set_{0};
};
static_assert(Bitset<uint32_t>::max_count == 32);
static_assert(Bitset<uint32_t>{}.rep() == 0u);
static_assert(Bitset<uint32_t>::fromRep(8u).contains(3));
static_assert(Bitset<uint32_t>::fromRep(15u).count() == 4u);
///@}

} // namespace Potassco
