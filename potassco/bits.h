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
//! Returns a value of T with bit @c n set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T bit_mask(unsigned n) {
    return static_cast<T>(1) << n;
}
static_assert(bit_mask<unsigned>(0) == 1u && bit_mask<unsigned>(3) == 8u);

//! Returns whether bit @c n is set in @c x.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_bit(T x, unsigned n) noexcept {
    return (x & bit_mask<T>(n)) != 0;
}
static_assert(test_bit(7u, 0) && not test_bit(8u, 4));
//! Returns a copy of @c x with bit @c n set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T set_bit(T x, unsigned n) noexcept {
    return x | bit_mask<T>(n);
}
static_assert(set_bit(6u, 0) == 7u && set_bit(8u, 1) == 10u);
//! Effect: x = set_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_set_bit(T& x, unsigned n) noexcept {
    return (x |= bit_mask<T>(n));
}
//! Returns a copy of @c x with bit @c n cleared.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T clear_bit(T x, unsigned n) noexcept {
    return x & ~bit_mask<T>(n);
}
static_assert(clear_bit(7u, 0) == 6u && clear_bit(8u, 3) == 0u);
//! Effect: x = clear_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_clear_bit(T& x, unsigned n) noexcept {
    return (x &= ~bit_mask<T>(n));
}
//! Returns a copy of @c x with bit @c n toggled.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T toggle_bit(T x, unsigned n) noexcept {
    return x ^ bit_mask<T>(n);
}
static_assert(toggle_bit(6u, 0) == 7u && toggle_bit(7u, 1) == 5u);
//! Effect: x = toggle_bit(x, n)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_toggle_bit(T& x, unsigned n) noexcept {
    return (x ^= bit_mask<T>(n));
}
///@}
/*!
 * Functions on bit masks
 */
///@{
//! Returns whether @c x has all set bits in the mask @c m set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_mask(T x, std::type_identity_t<T> m) noexcept {
    return (x & m) == m;
}
static_assert(test_mask(15u, 7u) && not test_mask(10u, 6u));
//! Returns whether @c x has any set bits in the mask @c m set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr bool test_any(T x, std::type_identity_t<T> m) noexcept {
    return (x & m) != 0;
}
static_assert(test_any(15u, 7u) && test_any(10u, 6u));
//! Returns a copy of @c x with all set bits in the mask @c m set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T set_mask(T x, std::type_identity_t<T> m) noexcept {
    return x | m;
}
static_assert(set_mask(6u, 3u) == 7u && set_mask(1024u, 7u) == 1031u);
//! Effect: x = set_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_set_mask(T& x, std::type_identity_t<T> m) noexcept {
    return (x |= m);
}
//! Returns a copy of @c x with all set bits in the mask @c m cleared.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T clear_mask(T x, std::type_identity_t<T> m) noexcept {
    return x & ~m;
}
static_assert(clear_mask(7u, 3u) == 4u && clear_mask(19u, 17u) == 2u);
//! Effect: x = clear_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_clear_mask(T& x, std::type_identity_t<T> m) noexcept {
    return (x &= ~m);
}
//! Returns a copy of @c x with all bits in the mask @c m toggled.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T toggle_mask(T x, std::type_identity_t<T> m) noexcept {
    return x ^ m;
}
//! Effect: x = toggle_mask(x, m)
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr T& store_toggle_mask(T& x, std::type_identity_t<T> m) noexcept {
    return (x ^= m);
}
///@}

//! Returns a copy of @c x with only the right most set bit set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T right_most_bit(T x) noexcept {
    POTASSCO_WARNING_PUSH()
    POTASSCO_WARNING_IGNORE_MSVC(4146) // unary minus operator applied to unsigned type, result still unsigned
    return x & (-x);
    POTASSCO_WARNING_POP()
}
static_assert(right_most_bit(0b00000000u) == 0b00000000u && right_most_bit(0b00010100u) == 0b00000100u);
//! Returns a copy of @c x with only the left most set bit set.
template <std::unsigned_integral T>
[[nodiscard]] POTASSCO_FORCE_INLINE constexpr T left_most_bit(T x) noexcept {
    return std::bit_floor(x);
}
static_assert(left_most_bit(0b00000000u) == 0b00000000u && left_most_bit(0b00010100u) == 0b00010000u);
//! Returns the log2 of @c x.
template <std::unsigned_integral T>
POTASSCO_FORCE_INLINE constexpr unsigned log2(T x) noexcept {
    return static_cast<unsigned>(std::bit_width(x)) - static_cast<unsigned>(x != 0u);
}
static_assert(log2(0u) == 0u && log2(1u) == 0u && log2(2u) == 1u && log2(4u) == 2u && log2(255u) == 7u);
///@}

} // namespace Potassco
