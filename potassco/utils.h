//
// Copyright (c) 2024 - present, Benjamin Kaufmann
//
// This file is part of Potassco.
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
#include <potassco/bits.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace Potassco {
namespace Detail {
//! (Forward) Iterator for enumerating over ranges.
template <typename R, typename SizeT>
class EnumIter {
public:
    using IterType        = std::ranges::iterator_t<R>;
    using IdxType         = std::conditional_t<std::is_same_v<SizeT, void>, std::ranges::range_size_t<R>, SizeT>;
    using value_type      = std::pair<IdxType, std::ranges::range_reference_t<R>>;
    using difference_type = std::ranges::range_difference_t<R>;
    EnumIter(IterType it = {}, IdxType i = 0) noexcept : current_(it), index_(i) {} // NOLINT
    //! Prefix increment.
    constexpr auto operator++() noexcept -> EnumIter& {
        ++current_;
        ++index_;
        return *this;
    }
    //! Postfix increment.
    constexpr auto operator++(int) noexcept -> EnumIter {
        auto tmp = *this;
        ++*this;
        return tmp;
    }
    //! Get current position and element.
    constexpr auto operator*() const noexcept -> value_type { return {index_, *current_}; }
    //! Equality comparison.
    friend constexpr bool operator==(const EnumIter& lhs, const EnumIter& rhs) noexcept {
        return lhs.current_ == rhs.current_;
    }

private:
    IterType current_;
    IdxType  index_ = 0;
};
} // namespace Detail
//! Returns a range adaptor similar to C++23's std::views::enumerate.
/*!
 * There are three major differences to the C++23 version:
 * 1. Only sized ranges are supported.
 * 2. The iterator type is only a forward iterator even if the underlying range provides a stronger iterator category.
 * 3. The index type is either an explicitly given type or the underlying range's size (and not its difference) type.
 */
template <typename SizeT = void, std::ranges::sized_range R>
constexpr auto enumerate(R&& r) {
    if constexpr (std::is_lvalue_reference_v<R>) {
        struct V {
            using RangeT = std::remove_reference_t<R>;
            using IterT  = Detail::EnumIter<RangeT, SizeT>;
            RangeT*            base;
            [[nodiscard]] auto begin() const noexcept { return IterT{std::ranges::begin(*base), 0}; }
            [[nodiscard]] auto end() const noexcept { return IterT{std::ranges::end(*base), 0}; }
        };
        return V{std::addressof(r)};
    }
    else {
        struct V {
            using RangeT = std::remove_cvref_t<R>;
            using IterT  = Detail::EnumIter<RangeT, SizeT>;
            RangeT             base;
            [[nodiscard]] auto begin() noexcept { return IterT{std::ranges::begin(base), 0}; }
            [[nodiscard]] auto end() noexcept { return IterT{std::ranges::end(base), 0}; }
        };
        return V{std::forward<R>(r)};
    }
}

/*!
 * \addtogroup BasicTypes
 */
///@{

//! A (dynamically sized) buffer of raw memory.
/*!
 * The class manages a (dynamically sized) buffer of memory obtained by malloc/realloc.
 * It uses a simple geometric scheme when the buffer needs to grow.
 */
class DynamicBuffer {
public:
    using trivially_relocatable = std::true_type; // NOLINT

    //! Creates a buffer with the given initial capacity.
    explicit DynamicBuffer(std::size_t initialCap = 0);
    explicit DynamicBuffer(std::span<char> borrow);
    ~DynamicBuffer();
    DynamicBuffer(const DynamicBuffer&);
    DynamicBuffer(DynamicBuffer&&) noexcept;
    DynamicBuffer& operator=(DynamicBuffer&&) noexcept;
    DynamicBuffer& operator=(const DynamicBuffer&);

    //! Returns the maximum size that the buffer may grow to without triggering reallocation.
    [[nodiscard]] uint32_t capacity() const noexcept { return cap_; }
    //! Returns the number of bytes used in this buffer.
    [[nodiscard]] uint32_t size() const noexcept { return sizeOwn_ & size_mask; }
    //! Returns a pointer to the beginning of the buffer.
    [[nodiscard]] char*            data() const noexcept { return static_cast<char*>(beg_); }
    [[nodiscard]] char*            data(std::size_t pos) const noexcept { return data() + pos; }
    [[nodiscard]] std::string_view view(std::size_t pos = 0, std::size_t n = std::string_view::npos) const {
        return {data() + pos, std::min(n, size() - pos)};
    }

    //! Increases the capacity of the buffer to a value that is greater or equal to `n`.
    void reserve(std::size_t n);

    //! Resizes the buffer to accommodate an additional `n` bytes at the end.
    /*!
     * If the current capacity is insufficient, this function grows the region by reallocating a new block of memory,
     * thereby invalidating all existing references into the region.
     *
     * \post <tt>size() >= n</tt>
     */
    [[nodiscard]] std::span<char> alloc(std::size_t n);
    void                          append(const void* what, std::size_t n);
    DynamicBuffer&                append(std::string_view str) {
        append(str.data(), str.size());
        return *this;
    }
    //! Appends the given character to the buffer.
    void  push(char c) { append(&c, 1); }
    char& back() { return data()[size() - 1]; }

    //! Reduces the number of used bytes in this region by `n`.
    void pop(std::size_t n) { sizeOwn_ -= n <= size() ? static_cast<uint32_t>(n) : size(); }
    //! Reduces the number of used bytes in this region to 0.
    void clear() { sizeOwn_ &= ~size_mask; }

    //! Swaps this and other.
    void swap(DynamicBuffer& other) noexcept;

    //! Releases all allocated memory in this region.
    /*!
     * \post <tt>size() == capacity() == 0</tt>
     */
    void release() noexcept;

private:
    static constexpr auto size_mask  = 0x7FFFFFFFu;
    static constexpr auto borrow_bit = 31u;
    void*                 beg_{nullptr};
    uint32_t              cap_{0};
    uint32_t              sizeOwn_{0};
};
inline void swap(DynamicBuffer& lhs, DynamicBuffer& rhs) noexcept { lhs.swap(rhs); }

class DynamicBitset {
public:
    using IndexType             = uint32_t;
    using trivially_relocatable = std::true_type; // NOLINT

    //! Creates an empty set.
    DynamicBitset() noexcept = default;
    //! Reserves space for at least `numBits`.
    void reserve(uint32_t numBits);
    //! Returns whether the set contains the given bit.
    [[nodiscard]] bool contains(IndexType bit) const {
        auto [w, p] = idx(bit);
        return w < words() && test_bit(data()[w], p);
    }
    //! Returns whether the set is empty.
    [[nodiscard]] bool empty() const noexcept { return buffer_.size() == 0; }
    //! Returns the number of elements in the set, i.e., the number of bits set.
    [[nodiscard]] auto count() const noexcept -> unsigned;
    //! Returns the smallest element in the set or 0 if empty.
    [[nodiscard]] auto smallest() const noexcept -> unsigned;
    //! Returns the largest element in the set or 0 if empty.
    [[nodiscard]] auto largest() const noexcept -> unsigned;
    //! Returns the number of active words.
    [[nodiscard]] auto words() const noexcept -> uint32_t { return buffer_.size() / sizeof(SetType); }
    //! Adds the given bit to the set and returns true if it was not already in the set.
    bool add(IndexType bit);
    //! Removes the given bit from the set and returns true if it was in the set.
    bool remove(IndexType bit);
    //! Removes all elements from the set.
    void clear() noexcept { buffer_.clear(); }
    //! Bitwise-ANDs all the bits in this bitset with the given mask.
    void apply(uint64_t mask);

    friend bool operator==(const DynamicBitset& lhs, const DynamicBitset& rhs) noexcept {
        return lhs.compare(rhs) == std::strong_ordering::equal;
    }
    friend auto operator<=>(const DynamicBitset& lhs, const DynamicBitset& rhs) noexcept { return lhs.compare(rhs); }

private:
    using SetType = uint64_t;
    struct Index {
        uint32_t word : 26;
        uint32_t bit  : 6;
    };
    [[nodiscard]] static constexpr auto idx(IndexType bit) -> Index { return {bit / 64u, bit & 63u}; }
    [[nodiscard]] auto                  compare(const DynamicBitset& rhs) const -> std::strong_ordering;
    [[nodiscard]] auto data() const noexcept -> SetType* { return reinterpret_cast<SetType*>(buffer_.data()); }
    void               compact();

    DynamicBuffer buffer_;
};

//! A trivially relocatable immutable string type with small buffer optimization.
/*!
 * Not all std::string implementations are trivially relocatable. E.g., the SSO implemented in gcc (libstdc++) relies on
 * a pointer referencing a buffer internal to the string, making relocation non-trivial.
 * In contrast, this class uses an SSO implementation that is more similar to the one from libc++.
 */
class ConstString final {
public:
    using trivially_relocatable = std::true_type; // NOLINT
    struct Borrow_t {};
    //! Creates an empty string.
    constexpr ConstString() noexcept {
        reset();
        if (std::is_constant_evaluated()) {
            std::fill(std::begin(storage_) + 1, std::end(storage_) - 1, static_cast<char>(0));
        }
    }
    //! Creates a string by copying `n`.
    explicit ConstString(std::string_view n);
    //! Creates a string by borrowing `n`.
    /*!
     * \note It is the caller's responsibility to ensure that the new object is only used as long as `n` is valid.
     */
    ConstString(Borrow_t, std::string_view n);
    //! Creates a (deep) copy of `o`.
    ConstString(const ConstString& o);
    //! "Steals" the content of `o`.
    constexpr ConstString(ConstString&& o) noexcept {
        if (o.small()) {
            if (std::is_constant_evaluated()) {
                std::copy(std::begin(o.storage_), std::end(o.storage_), storage_);
            }
            else {
                std::memmove(storage_, o.storage_, o.size() + 1);
                storage_[c_max_small] = o.storage_[c_max_small];
            }
        }
        else {
            new (storage_) Large{*o.large()};
            storage_[c_max_small] = o.storage_[c_max_small];
        }
        o.reset();
    }
    constexpr ~ConstString() {
        if (tag() == c_large_tag) {
            release();
        }
    }
    ConstString& operator=(const ConstString& other);
    ConstString& operator=(ConstString&& other) noexcept;

    //! Converts this string to a string_view.
    [[nodiscard]] constexpr explicit operator std::string_view() const { return {c_str(), size()}; }
    //! Returns this string as a null-terminated C string.
    [[nodiscard]] constexpr const char* c_str() const { return small() ? storage_ : large()->str; }
    //! Converts this string to a string_view.
    [[nodiscard]] constexpr std::string_view view() const { return static_cast<std::string_view>(*this); }
    //! Returns the length of this string.
    [[nodiscard]] constexpr std::size_t size() const { return small() ? c_max_small - tag() : large()->size; }
    //! Returns the character at the given position, which shall be \< `size()`.
    [[nodiscard]] constexpr char operator[](std::size_t pos) const { return c_str()[pos]; }

    [[nodiscard]] constexpr bool small() const { return tag() < c_large_tag; }

    friend bool operator==(const ConstString& lhs, const ConstString& rhs) { return lhs.view() == rhs.view(); }
    friend auto operator<=>(const ConstString& lhs, const ConstString& rhs) { return lhs.view() <=> rhs.view(); }
    friend bool operator==(std::string_view lhs, const ConstString& rhs) { return lhs == rhs.view(); }
    friend auto operator<=>(std::string_view lhs, const ConstString& rhs) { return lhs <=> rhs.view(); }

private:
    static constexpr auto c_max_small  = 23u;
    static constexpr auto c_large_tag  = c_max_small + 1u;
    static constexpr auto c_borrow_tag = c_large_tag + 1u;
    struct Large {
        const char* str;
        std::size_t size;
    };
    void                            init(std::string_view str);
    [[nodiscard]] const Large*      large() const { return reinterpret_cast<const Large*>(storage_); }
    [[nodiscard]] constexpr uint8_t tag() const { return static_cast<uint8_t>(storage_[c_max_small]); }
    void                            release();
    constexpr void                  reset() {
        storage_[0]           = 0;
        storage_[c_max_small] = static_cast<char>(c_max_small);
    }
    alignas(Large) char storage_[c_max_small + 1];
};
static_assert(ConstString{}.small());
static_assert(ConstString{ConstString{}}.small());
template <typename ValueType>
using StringMap = std::unordered_map<ConstString, ValueType, std::hash<ConstString>, std::equal_to<>>;

template <typename ValueType, typename... Args>
auto try_emplace(StringMap<ValueType>& map, std::string_view key,
                 Args&&... args) -> std::pair<typename StringMap<ValueType>::iterator, bool> {
    // Create a "borrowed" key for lookup. If `key` is not in map, `try_emplace()` will copy-construct a key from `k`,
    // thereby materializing a full string. Otoh, if `map` already contains an element with the given key, we avoid an
    // unnecessary allocation.
    // NOTE: Once we have C++26 and the heterogeneous version of `try_emplace()`, this workaround can be removed.
    ConstString k{ConstString::Borrow_t{}, key};
    return map.try_emplace(k, std::forward<Args>(args)...);
}

///@}

} // namespace Potassco
template <>
struct std::hash<Potassco::ConstString> : std::hash<std::string_view> {
    using is_transparent = void; // NOLINT
    using std::hash<std::string_view>::operator();
    std::size_t operator()(const Potassco::ConstString& str) const noexcept { return (*this)(str.view()); }
};
