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
#include <potassco/basic_types.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Potassco {
template <typename T>
concept HasTriviallyRelocatable = requires() {
    typename T::trivially_relocatable;
    requires T::trivially_relocatable::value;
};
template <typename T>
struct is_trivially_relocatable // NOLINT
    : std::integral_constant<bool, std::is_trivially_copyable_v<T> || HasTriviallyRelocatable<T>> {};

template <typename T, typename U>
struct is_trivially_relocatable<std::pair<T, U>>
    : std::integral_constant<bool, is_trivially_relocatable<T>::value && is_trivially_relocatable<U>::value> {};

template <typename T>
struct is_trivially_relocatable<std::unique_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;

namespace Detail {
template <typename T>
using Param_t = std::conditional_t<std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(void*) * 2, T, const T&>;
//! Forward iterator for enumerating over ranges.
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
    //! Get the current position and element.
    constexpr auto operator*() const noexcept -> value_type { return {index_, *current_}; }
    //! Equality comparison.
    friend constexpr bool operator==(const EnumIter& lhs, const EnumIter& rhs) noexcept {
        return lhs.current_ == rhs.current_;
    }

private:
    IterType current_;
    IdxType  index_ = 0;
};

template <std::integral K, K Empty>
struct NumHashTraits {
    using key_type = K; // NOLINT
    static auto empty() noexcept -> key_type { return Empty; }
    static auto hashKey(key_type key) noexcept -> uint32_t {
        if constexpr (sizeof(key_type) <= sizeof(uint32_t)) {
            return static_cast<uint32_t>(static_cast<std::make_unsigned_t<key_type>>(key) * 37u);
        }
        else {
            auto x  = static_cast<uint64_t>(key);
            x      *= 0xbf58476d1ce4e5b9u;
            x      ^= x >> 31;
            return static_cast<uint32_t>(x);
        }
    }
};
inline constexpr auto fast_grow_cap = 0x20000u;
inline constexpr auto min_grow_cap  = 64u;
template <std::unsigned_integral SizeT>
constexpr auto nextCap(SizeT current, std::size_t elemSize) -> SizeT {
    auto minCap = static_cast<SizeT>(std::max(min_grow_cap, static_cast<unsigned>(elemSize)) / elemSize);
    if (current < minCap) {
        return minCap;
    }
    if (auto nc = current > 8u && current <= (fast_grow_cap / elemSize) ? (current * 3 + 1) >> 1 : current << 1u;
        nc > current) {
        return nc;
    }
    return static_cast<SizeT>(static_cast<SizeT>(-1) / elemSize);
}

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
    [[nodiscard]] auto capacity() const noexcept -> uint32_t { return cap_; }
    //! Returns the number of bytes used in this buffer.
    [[nodiscard]] auto size() const noexcept -> uint32_t { return sizeOwn_ & size_mask; }
    //! Returns a pointer to the beginning of the buffer.
    [[nodiscard]] char* data() const noexcept { return static_cast<char*>(beg_); }
    [[nodiscard]] char* data(std::size_t pos) const noexcept { return data() + pos; }
    [[nodiscard]] auto  view(std::size_t pos = 0, std::size_t n = std::string_view::npos) const -> std::string_view {
        return {data() + pos, std::min(n, size() - pos)};
    }
    [[nodiscard]] static auto maxSize() noexcept -> uint32_t { return size_mask; }

    //! Increases the capacity of the buffer to a value that is greater or equal to `n`.
    void reserve(std::size_t n) {
        if (n > capacity()) {
            grow(n, true);
        }
    }

    //! Resizes the buffer to accommodate an additional `n` bytes at the end.
    /*!
     * If the current capacity is insufficient, this function grows the region by reallocating a new block of memory,
     * thereby invalidating all existing references into the region.
     *
     * \post <tt>size() >= n</tt>
     */
    [[nodiscard]] auto alloc(std::size_t n) -> std::span<char>;
    void               append(const void* what, std::size_t n);
    auto               append(std::string_view str) -> DynamicBuffer& {
        append(str.data(), str.size());
        return *this;
    }
    //! Appends the given character to the buffer.
    void push(char c) {
        auto sz = size();
        if (size() == capacity()) {
            grow(sz + 1, false);
        }
        data()[sz] = c;
        ++sizeOwn_;
    }
    auto back() -> char& { return data()[size() - 1]; }

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
    //
    void grow(std::size_t n, bool exact = false);

    void*    beg_{nullptr};
    uint32_t cap_{0};
    uint32_t sizeOwn_{0};
};
inline void swap(DynamicBuffer& lhs, DynamicBuffer& rhs) noexcept { lhs.swap(rhs); }

template <typename T>
requires(is_trivially_relocatable_v<T> && alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
class DynamicArray {
public:
    // NOLINTBEGIN
    using size_type             = uint32_t;
    using pointer               = T*;
    using const_pointer         = const T*;
    using iterator              = pointer;
    using const_iterator        = const_pointer;
    using reference             = T&;
    using const_reference       = const T&;
    using value_type            = T;
    using value_type_param      = Detail::Param_t<value_type>;
    using trivially_relocatable = std::true_type;
    // NOLINTEND
    DynamicArray() = default;
    DynamicArray(const DynamicArray& other) : DynamicArray() { append(other.begin(), other.end()); }
    DynamicArray(DynamicArray&&) noexcept = default;
    ~DynamicArray() { clear(); }
    auto operator=(const DynamicArray& other) -> DynamicArray& {
        if (this != &other) {
            reset();
            append(other.begin(), other.end());
        }
        return *this;
    }
    auto operator=(DynamicArray&&) noexcept -> DynamicArray& = default;

    [[nodiscard]] auto begin() const noexcept -> const_iterator { return data(); }
    [[nodiscard]] auto begin() noexcept -> iterator { return data(); }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return data() + size(); }
    [[nodiscard]] auto end() noexcept -> iterator { return data() + size(); }
    [[nodiscard]] auto data() const noexcept -> const_pointer { return reinterpret_cast<const_pointer>(buf_.data()); }
    [[nodiscard]] auto data() noexcept -> pointer { return reinterpret_cast<pointer>(buf_.data()); }

    [[nodiscard]] auto operator[](size_type pos) const -> const_reference {
        return const_cast<DynamicArray&>(*this)[pos];
    }
    [[nodiscard]] auto operator[](size_type pos) -> reference {
        assert(pos < size());
        return data()[pos];
    }
    [[nodiscard]] auto at(size_type pos) const -> const_reference { return const_cast<DynamicArray&>(*this).at(pos); }
    [[nodiscard]] auto at(size_type pos) -> reference {
        if (pos < size()) {
            return data()[pos];
        }
        throw std::out_of_range("DynamicArray::at()");
    }

    [[nodiscard]] auto front() const -> const_reference { return (*this)[0]; }
    [[nodiscard]] auto front() -> reference { return (*this)[0]; }
    [[nodiscard]] auto back() const -> const_reference { return (*this)[size() - 1]; }
    [[nodiscard]] auto back() -> reference { return (*this)[size() - 1]; }

    [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0u; }
    [[nodiscard]] auto size() const noexcept -> size_type { return buf_.size() / val_size; }
    [[nodiscard]] auto capacity() const noexcept -> size_type { return buf_.capacity() / val_size; }
    [[nodiscard]] auto maxSize() const noexcept -> size_type { return buf_.maxSize() / val_size; /* NOLINT*/ }

    void push_back(value_type_param u) {
        push(1u, [&u](T* pos) { std::construct_at(pos, u); });
    }
    void push_back(T&& u) requires(not std::is_same_v<value_type_param, T>)
    {
        push(1u, [&u](T* pos) { std::construct_at(pos, std::move(u)); });
    }
    template <typename... Args>
    void emplace_back(Args&&... args) {
        push(1u, [&](T* pos) { std::construct_at(pos, std::forward<Args>(args)...); });
    }
    void pop_back() { pop(1u); }
    void pop(size_type n) {
        if (n) {
            destroy(end() - n, n);
            buf_.pop(valSize(n));
        }
    }
    template <typename It>
    requires(not std::integral<It>)
    void append(It first, It last) {
        auto n = static_cast<std::size_t>(std::distance(first, last));
        assert(std::cmp_less_equal(n, maxSize() - size()));
        push(static_cast<size_type>(n), [&first, n](T* pos) { std::uninitialized_copy_n(first, n, pos); });
    }
    void append(size_type n, value_type_param val) {
        push(n, [&val, n](T* pos) { std::uninitialized_fill_n(pos, n, val); });
    }

    void reserve(size_type nc) {
        if (nc > capacity()) {
            buf_.reserve(valSize(nc));
            assert(capacity() == nc);
        }
    }

    void resize(size_type count, const T& val = T()) {
        if (auto sz = size(); count > sz) {
            append(count - sz, val);
        }
        else {
            pop(sz - count);
        }
        assert(size() == count);
    }

    void clear() {
        destroy(data(), size());
        buf_.clear();
    }

    //! Swaps this and other.
    void swap(DynamicArray& other) noexcept { buf_.swap(other.buf_); }

    void reset() {
        destroy(data(), size());
        buf_.release();
    }

private:
    static constexpr auto val_size = static_cast<size_type>(sizeof(T));
    static constexpr auto valSize(size_type n) noexcept { return static_cast<std::size_t>(n * val_size); }

    void grow(size_type n) {
        auto nc = std::max(Detail::nextCap(capacity(), val_size), size() + n);
        buf_.reserve(valSize(nc));
        assert(capacity() == nc);
    }
    template <typename Op>
    void push(size_type n, Op op) {
        if (std::cmp_greater(size() + n, capacity())) {
            grow(n);
        }
        auto* mem = reinterpret_cast<pointer>(buf_.alloc(valSize(n)).data());
        try {
            std::move(op)(mem);
        }
        catch (...) {
            buf_.pop(valSize(n));
            throw;
        }
    }
    POTASSCO_ATTR_INLINE void destroy(pointer first, size_type n) {
        if constexpr (not std::is_trivially_destructible_v<T>) {
            std::destroy_n(first, n);
        }
    }
    DynamicBuffer buf_;
};
template <typename T>
void swap(DynamicArray<T>& lhs, DynamicArray<T>& rhs) noexcept {
    lhs.swap(rhs);
}
template <std::equality_comparable T>
auto operator==(const DynamicArray<T>& lhs, const DynamicArray<T>& rhs) -> bool {
    return std::ranges::equal(lhs, rhs);
}
template <std::three_way_comparable T>
auto operator<=>(const DynamicArray<T>& lhs, const DynamicArray<T>& rhs)
    -> decltype(std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end())) {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

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

//! Enumeration type for guiding hash probe lookup.
enum class HashProbeResult {
    success,   //!< Element found - stop probing.
    fail,      //!< Empty element found - stop probing.
    collision, //!< Probe hit a valid element, but probing should continue.
    removed,   //!< Probe hit a removed element, which might be returned if no other element is found.
};

template <typename TraitsT, typename T>
concept HashArrayTraits = requires(const T& x) {
    typename TraitsT::size_type;
    typename TraitsT::hash_type;
    { TraitsT::used(x) } -> std::convertible_to<bool>;
    { TraitsT::hashKey(x) } -> std::same_as<typename TraitsT::hash_type>;
};
//! A (dynamically sized) array type intended to be used as a foundation for linear probing hash tables.
template <typename T, HashArrayTraits<T> TraitsT>
class DynamicHashArray {
public:
    using pointer   = T*;                          // NOLINT
    using size_type = typename TraitsT::size_type; // NOLINT
    using hash_type = typename TraitsT::hash_type; // NOLINT

    DynamicHashArray() = default;
    explicit DynamicHashArray(size_type bucketCount) {
        if (bucketCount) {
            if (auto cap = std::bit_ceil(std::max(bucketCount, static_cast<size_type>(8u))); cap >= bucketCount) {
                auto t = std::make_unique<T[]>(cap);
                arr_   = {t.release(), cap};
            }
            else {
                throw std::length_error{"DynamicHashArray"};
            }
        }
    }
    DynamicHashArray(DynamicHashArray&& other) noexcept : arr_(std::exchange(other.arr_, {})) {}
    DynamicHashArray& operator=(DynamicHashArray&& other) noexcept {
        if (data() != other.data()) {
            Deleter{}(arr_.data());
            arr_ = std::exchange(other.arr_, {});
        }
        return *this;
    }
    ~DynamicHashArray() noexcept { Deleter{}(arr_.data()); }

    //! Clears the array while keeping its capacity().
    void clear() { std::fill_n(data(), capacity(), T{}); }

    //! Returns the array's current capacity.
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
        return static_cast<size_type>(std::size(arr_));
    }
    //! Returns a pointer to the internal array.
    [[nodiscard]] constexpr auto data() const noexcept -> pointer { return arr_.data(); }
    //! Returns the array's current hash mask.
    [[nodiscard]] constexpr auto mask() const noexcept -> size_type { return capacity() - 1; }
    //! Returns the element at the given array index.
    [[nodiscard]] constexpr auto operator[](size_type i) -> T& { return arr_[i]; }

    //! Doubles the capacity of this array and relocates all "used" elements.
    auto grow() -> void {
        auto tmp = DynamicHashArray(std::max(capacity() * 2u, static_cast<size_type>(1u)));
        for (auto& b : arr_) {
            if (TraitsT::used(b)) {
                *tmp.nextUnused(TraitsT::hashKey(b)) = std::move(b);
            }
        }
        Deleter{}(arr_.data());
        arr_ = std::exchange(tmp.arr_, {});
    }

    //! Returns the first position in the array that could store an element with the given hash.
    /*!
     * \note If capacity() is 0, the function returns (nullptr, false).
     * \param hash The hash to lookup.
     * \param pred The probe predicate to apply on each visited element. Search is stopped once this predicate returns
     *             a "terminating" probe result, i.e., HashProbeResult::success, or HashProbeResult::fail.
     * \return A pair storing the position where an entry with the given hash should be stored according to the provided
     *         predicate and a boolean indicating whether the search stopped with HashProbeResult::success.
     */
    template <typename Pred>
    requires(std::is_invocable_r_v<HashProbeResult, Pred, T>)
    [[nodiscard]] auto lookup(hash_type hash, const Pred& pred) const -> std::pair<pointer, bool> {
        auto ret = std::pair<pointer, bool>{nullptr, false};
        if (capacity()) {
            const auto m = mask();
            for (pointer data = arr_.data();; ++hash) {
                auto b = hash & m;
                if (auto r = pred(data[b]); r != HashProbeResult::collision) {
                    if (not ret.first || r == HashProbeResult::success) {
                        ret = {&data[b], r == HashProbeResult::success};
                    }
                    if (r != HashProbeResult::removed) {
                        break;
                    }
                }
            }
        }
        return ret;
    }

    //! Returns the first unused position in the array starting from the home position for the given hash.
    /*!
     * \pre capacity() > 0
     */
    [[nodiscard]] auto nextUnused(hash_type hash) noexcept -> pointer {
        for (const auto& m = mask();; ++hash) {
            if (auto pos = hash & m; not TraitsT::used(arr_[pos])) {
                return &arr_[pos];
            }
        }
    }

    //! Erases the given element via "Deletion with linear probing" (Algorithm R) from Knuth's TAOCP.
    void erase(pointer pos) {
        assert(pos != nullptr);
        auto hole = static_cast<uint32_t>(pos - data());
        assert(hole < capacity());
        const auto m = mask();
        for (auto j = hole + 1; TraitsT::used(arr_[j &= m]); ++j) {
            const auto home = TraitsT::hashKey(arr_[j]);
            const auto lhs  = ((hole - home) & m);
            const auto rhs  = ((j - home) & m);
            // If hole precedes j on j's linear probe chain starting from its home position, we
            // move j to the hole and make j's old position the new hole.
            if (lhs < rhs) {
                arr_[hole] = std::move(arr_[j]);
                hole       = j;
            }
        }
        arr_[hole] = {};
    }

private:
    using Deleter   = typename std::unique_ptr<T[]>::deleter_type;
    using ArrayType = std::span<T>;
    ArrayType arr_;
};

//! A (dynamically sized) id index implemented as an open-addressing hashtable.
/*!
 * \note The index does not store values, but instead is meant as an index atop an existing external data container.
 * \note The index assumes that valid ids are < `id_max-1` and uses `id_max` and `id_max-1` as sentinel values.
 * \note The index itself does not compute hash values. It is the responsibility of the owner of the external data
 *       to compute hashes for elements to be indexed. However, the class stores the hashes so that it can
 *       grow the index when it becomes too full. Furthermore, stored hashes are also used during lookup to skip
 *       elements without having to recompute their hash.
 */
class DynamicIndex {
    static constexpr auto id_empty = id_max;
    static constexpr auto id_tomb  = id_empty - 1;
    struct Bucket {
        using size_type = uint32_t; // NOLINT
        using hash_type = uint32_t; // NOLINT
        [[nodiscard]] constexpr auto        used() const noexcept { return value < id_tomb; }
        [[nodiscard]] static constexpr auto used(const Bucket& b) noexcept -> bool { return b.used(); }
        [[nodiscard]] static constexpr auto hashKey(const Bucket& b) noexcept -> uint32_t { return b.hash; }

        uint32_t hash{0u};
        Id_t     value{id_empty};
    };

public:
    using HashType = Bucket::hash_type;

    using trivially_relocatable = std::true_type; // NOLINT

    //! Creates an empty index.
    DynamicIndex() = default;
    //! Destroys an index.
    ~DynamicIndex();
    //! Creates an index with at least `bucketCount` buckets using `lf` as the max load factor.
    /*!
     * \pre `lf >= 0.5` and `lf < 1.0`.
     */
    explicit DynamicIndex(uint32_t bucketCount, double lf = 0.85);
    //! Creates a copy of `other`.
    DynamicIndex(const DynamicIndex& other);
    //! Move-constructs the index from `other`.
    DynamicIndex(DynamicIndex&& other) noexcept;
    //! Replaces this index with a copy of `other`.
    DynamicIndex& operator=(const DynamicIndex& other);
    //! Replaces this index with `other`.
    DynamicIndex& operator=(DynamicIndex&& other) noexcept;

    //! Returns the number of elements in the index.
    [[nodiscard]] constexpr auto size() const noexcept -> uint32_t { return size_; }
    //! Returns whether the index is empty.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0u; }
    //! Returns whether the index is full and therefore will grow when the next element is added.
    [[nodiscard]] constexpr auto full() const noexcept -> bool { return grow_ == 0u; }
    //! Returns the number of buckets in the index.
    [[nodiscard]] constexpr auto buckets() const noexcept -> uint32_t { return table_.capacity(); }

    //! A type for storing the result of an index lookup.
    class IndexRef {
    public:
        //! Creates an "invalid" reference.
        constexpr IndexRef() = default;
        //! Returns whether the object references a valid index entry.
        [[nodiscard]] constexpr bool valid() const noexcept { return pos_ && pos_->used(); }
        //! Returns the id of the referenced element or `id_max` if this reference is not valid.
        [[nodiscard]] constexpr auto operator*() const noexcept -> Id_t { return valid() ? pos_->value : id_empty; }
        //! Returns `valid()`.
        constexpr explicit operator bool() const noexcept { return valid(); }
        // For testing only
        [[nodiscard]] constexpr auto bucket() const noexcept -> const Bucket* { return pos_; }

    private:
        friend class DynamicIndex;
        constexpr explicit IndexRef(const Bucket* p) : pos_(p) {}
        const Bucket* pos_{nullptr};
    };

    //! Returns a reference to the first element with the given hash for which the provided predicate returns true.
    /*!
     * If the index does not contain an element with the given hash or the provided predicate returns false for
     * all elements with a matching hash, the function returns an "invalid" reference.
     *
     * \note An "invalid" result can later be used when adding the missing element.
     */
    template <typename CmpFunc>
    requires(std::is_invocable_r_v<bool, CmpFunc, Id_t>)
    [[nodiscard]] auto find_if(HashType hash, CmpFunc&& func) const noexcept -> IndexRef {
        return IndexRef{table_
                            .lookup(hash,
                                    [&](const Bucket& e) {
                                        if (not e.used()) {
                                            return e.value == id_empty ? HashProbeResult::fail
                                                                       : HashProbeResult::removed;
                                        }
                                        return e.hash == hash && func(e.value) ? HashProbeResult::success
                                                                               : HashProbeResult::collision;
                                    })
                            .first};
    }
    [[nodiscard]] auto find_if(HashType hash, Id_t id) const noexcept -> IndexRef {
        return find_if(hash, [id](Id_t x) { return x == id; });
    }

    //! Returns whether the index contains an element with the given hash for which the provided predicate returns true.
    template <typename CmpFunc>
    requires(std::is_invocable_r_v<bool, CmpFunc, Id_t>)
    [[nodiscard]] auto contains(HashType hash, CmpFunc&& func) const noexcept -> bool {
        return find_if(hash, std::forward<CmpFunc>(func)).valid();
    }
    [[nodiscard]] auto contains(HashType hash, Id_t id) const noexcept -> bool { return find_if(hash, id).valid(); }

    //! Adds a new entry to this index at the given position.
    /*!
     * \note The function assumes that a corresponding element is not yet in the index.
     * \param pos An "invalid" reference obtained by a call to `find_if` for the element.
     * \param hash The hash of the entry with the given id.
     * \param id The id of the entry in the external container.
     * \pre id < `id_max-1`.
     */
    void add(IndexRef pos, HashType hash, Id_t id) {
        if (full()) {
            grow();
            pos.pos_ = nullptr;
        }
        auto* bucket  = pos.pos_ != nullptr ? const_cast<Bucket*>(pos.pos_) : table_.nextUnused(hash);
        grow_        -= (bucket->value == id_empty);
        tombs_       -= (bucket->value == id_tomb);
        ++size_;
        *bucket = {hash, id};
    }

    //! Adds the given entry to the index provided that it does not yet exist.
    bool try_add(HashType hash, Id_t id) {
        if (auto found = find_if(hash, id); not found) {
            add(found, hash, id);
            return true;
        }
        return false;
    }

    //! Removes the element identified by `r` from the index or returns false if `r` does not reference an element.
    bool erase(IndexRef r);
    //! Removes all elements from the index but keeps the buckets.
    void clear();
    //! Removes all elements from the index and de-allocates all memory.
    void discard();

private:
    void grow();
    using BucketArray = DynamicHashArray<Bucket, Bucket>;
    BucketArray table_;
    uint32_t    size_{0u};
    uint32_t    grow_{0u};
    uint32_t    tombs_{0u};
    float       lf_{0.85f};
};

template <typename TraitsT, typename K>
concept HashTableKeyTraits = requires(const K& x) {
    { TraitsT::hashKey(x) } -> std::unsigned_integral;
    { TraitsT::empty() } -> std::same_as<K>;
};

//! A simple (linear-probing) hash table.
/*!
 * \note Entries are stored as (key,value)-pairs in a dynamic array. Hence, mutating operations invalidate
 *       existing references/pointers.
 */
template <typename KeyT, typename ValT, HashTableKeyTraits<KeyT> HashTraits, bool Relocatable>
class DynamicHashTable {
public:
    using key_type = KeyT; // NOLINT
    using map_type = ValT; // NOLINT
    struct BucketT {
        friend bool                                      operator==(const BucketT&, const BucketT&) = default;
        key_type                                         key{HashTraits::empty()};
        POTASSCO_ATTR_NO_UNIQUE_ADDRESS mutable map_type value{};
    };
    struct TraitsT {
        using size_type = uint32_t; // NOLINT
        using hash_type = uint32_t; // NOLINT
        static constexpr bool used(const BucketT& x) { return x.key != HashTraits::empty(); }
        static constexpr auto hashKey(const BucketT& x) -> hash_type {
            return static_cast<uint32_t>(HashTraits::hashKey(x.key));
        }
    };
    using ArrayType = DynamicHashArray<BucketT, TraitsT>;
    using pointer   = typename ArrayType::pointer;   // NOLINT
    using size_type = typename ArrayType::size_type; // NOLINT

    using trivially_relocatable = std::integral_constant<bool, Relocatable>; // NOLINT
    using const_pointer         = const BucketT*;                            // NOLINT

    //! Creates an empty table.
    DynamicHashTable() = default;
    //! Creates a table with at least `bucketCount` buckets.
    explicit DynamicHashTable(size_type bucketCount) : table_(bucketCount) {
        avail_ = static_cast<uint32_t>(table_.capacity() * 0.75);
    }
    //! Creates a copy of `other`.
    DynamicHashTable(const DynamicHashTable& other) : DynamicHashTable(other.size()) {
        auto todo = other.size();
        assert(avail_ >= todo);
        for (const auto* x = other.table_.data(); todo; ++x) {
            if (TraitsT::used(*x)) {
                *table_.nextUnused(TraitsT::hashKey(*x)) = *x;
                --todo;
            }
        }
        size_   = other.size_;
        avail_ -= size_;
    }
    //! Move-constructs the table from `other`.
    DynamicHashTable(DynamicHashTable&& other) noexcept
        : table_(std::move(other.table_))
        , size_(std::exchange(other.size_, 0u))
        , avail_(std::exchange(other.avail_, 0u)) {}
    //! Replaces this table with a copy of `other`.
    DynamicHashTable& operator=(const DynamicHashTable& other) {
        if (this != &other) {
            *this = DynamicHashTable(other);
        }
        return *this;
    }
    //! Replaces this map with `other`.
    DynamicHashTable& operator=(DynamicHashTable&& other) noexcept {
        if (this != &other) {
            table_ = std::move(other.table_);
            size_  = std::exchange(other.size_, 0u);
            avail_ = std::exchange(other.avail_, 0u);
        }
        return *this;
    }
    ~DynamicHashTable() = default;

    //! Returns the number of used elements in the table.
    [[nodiscard]] constexpr auto size() const noexcept -> uint32_t { return size_; }
    //! Returns whether the table has no used elements.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0u; }
    //! Returns the number of unused elements that can be used before the table needs to grow.
    [[nodiscard]] constexpr auto avail() const noexcept -> uint32_t { return avail_; }
    //! Returns whether the table contains the given key.
    template <typename K>
    [[nodiscard]] auto contains(K&& key) const noexcept -> bool {
        return findKey(std::forward<K>(key)) != nullptr;
    }
    //! Returns a pointer to the element with the given key or nullptr if no such element exists.
    template <typename K>
    [[nodiscard]] auto findKey(K&& key) const -> const_pointer {
        auto [pos, found] = lookup(HashTraits::hashKey(key), std::forward<K>(key));
        return found ? pos : nullptr;
    }
    //! Returns a view over the underlying table.
    [[nodiscard]] constexpr auto array() const noexcept -> std::span<const BucketT> {
        return {table_.data(), table_.capacity()};
    }

    //! Adds a new entry with the given key to the table if the key does not yet exist.
    /*!
     * \note If the key is already in the table, the function returns a pointer to the corresponding entry without
     *       changing its value (if any).
     * \param key The key to add.
     * \param args Arguments to be used for creating a value if necessary.
     * \return A pointer to the stored value and a boolean indicating whether a new entry was added.
     */
    template <typename K, typename... Args>
    auto add(K&& key, Args&&... args) -> std::pair<const_pointer, bool> {
        auto h            = HashTraits::hashKey(key);
        auto [pos, found] = lookup(h, key);
        if (found) {
            return {pos, false};
        }
        if (avail_ == 0) {
            table_.grow();
            avail_ = static_cast<uint32_t>(table_.capacity() * 0.75) - size_;
            pos    = table_.nextUnused(h);
        }
        KeyT nk{std::forward<K>(key)};
        assert(nk != HashTraits::empty());
        pos->key   = std::move(nk);
        pos->value = ValT{std::forward<Args>(args)...};
        --avail_;
        ++size_;
        return {pos, true};
    }

    //! Erases the given element.
    /*!
     * \param pos The element to erase.
     * \return A boolean indicating whether the element was erased.
     */
    bool erase(const_pointer pos) {
        if (pos && TraitsT::used(*pos)) {
            table_.erase(const_cast<pointer>(pos));
            --size_;
            ++avail_;
            return true;
        }
        return false;
    }
    //! Removes the element with the given key from the table.
    /*!
     * \param key The key to remove.
     * \return A boolean indicating whether the key was removed.
     * \post findKey(key) == nullptr.
     */
    template <typename K>
    bool remove(K&& key) {
        return erase(findKey(std::forward<K>(key)));
    }

    //! Removes all elements from this table but keeps the buckets.
    void clear() {
        avail_ += size_;
        size_   = 0u;
        table_.clear();
    }

private:
    template <typename K>
    auto lookup(typename ArrayType::hash_type hash, K&& key) const noexcept -> std::pair<pointer, bool> {
        return table_.lookup(hash, [&](const BucketT& val) {
            return not TraitsT::used(val) ? HashProbeResult::fail
                   : val.key == key       ? HashProbeResult::success
                                          : HashProbeResult::collision;
        });
    }
    ArrayType table_;
    uint32_t  size_{0u};
    uint32_t  avail_{0u};
};

//! A simple (linear-probing) hash map that maps integral keys to scalar values.
template <std::integral KeyT, typename ValueT, KeyT Empty>
requires(std::is_scalar_v<ValueT>)
using SimpleHashMap = DynamicHashTable<KeyT, ValueT, Detail::NumHashTraits<KeyT, Empty>, true>;

//! A trivially relocatable immutable string type with small buffer optimization.
/*!
 * Not all std::string implementations are trivially relocatable. E.g., the SSO implemented in gcc (libstdc++) relies on
 * a pointer referencing a buffer internal to the string, making relocation non-trivial.
 * In contrast, this class uses an SSO implementation that is more similar to the one from libc++.
 * By nature, a ConstString can't be changed once constructed. Hence, ConstString only maintains its allocated size,
 * but no additional "capacity". The size of a ConstString object is always 16-bytes (enough to store a pointer and a
 * size), with an SSO length of 15 characters (+1 for the null-terminator).
 */
class ConstString final {
public:
    using trivially_relocatable = std::true_type; // NOLINT
    struct Borrow_t {};
    struct NoSso_t {};
    //! Creates an empty string.
    constexpr ConstString() noexcept { // NOLINT(cppcoreguidelines-pro-type-member-init)
        if (std::is_constant_evaluated()) {
            std::fill_n(storage_, sizeof(storage_) - 1, static_cast<char>(0));
            storage_[c_max_small] = c_max_small;
        }
        else {
            reset();
        }
    }
    //! Creates a string by copying `n`.
    constexpr explicit ConstString(std::string_view n) : ConstString(true, n) {}
    //! Creates a string by copying `n`.
    /*!
     * \note This constructor disables SSO even if `n` fits into the small buffer.
     */
    ConstString(NoSso_t, std::string_view n) : ConstString(false, n) {}
    //! Creates a string by borrowing `n`.
    /*!
     * \note It is the caller's responsibility to ensure that the new object is only used as long as `n` is valid.
     */
    ConstString(Borrow_t, std::string_view n);
    //! Creates a (deep) copy of `o`.
    ConstString(const ConstString& o);
    //! "Steals" the content of `o`.
    constexpr ConstString(ConstString&& o) noexcept { // NOLINT(cppcoreguidelines-pro-type-member-init)
        moveFrom(std::move(o));
    }
    constexpr ~ConstString() { release(); }
    ConstString&           operator=(const ConstString& other);
    constexpr ConstString& operator=(ConstString&& other) noexcept {
        if (this != &other) {
            release();
            moveFrom(std::move(other));
        }
        return *this;
    }

    //! Converts this string to a string_view.
    [[nodiscard]] constexpr explicit operator std::string_view() const { return view(); }
    //! Returns this string as a null-terminated C string.
    [[nodiscard]] constexpr auto c_str() const -> const char* { return data(); }
    //! Returns this string as a string_view.
    [[nodiscard]] constexpr auto view() const -> std::string_view {
        return small() ? std::string_view{storage_, c_max_small - tag()} : large()->view();
    }
    //! Returns the length of this string.
    [[nodiscard]] constexpr auto size() const -> std::size_t { return small() ? c_max_small - tag() : large()->size; }
    //! Returns a pointer to the underlying character array.
    [[nodiscard]] constexpr auto data() const -> const char* { return small() ? storage_ : large()->str; }
    //! Returns the character at the given position, which shall be \<= `size()`.
    [[nodiscard]] constexpr char operator[](std::size_t pos) const { return data()[pos]; }
    //! Returns whether the string is stored within the object's SSO buffer.
    [[nodiscard]] constexpr bool small() const { return tag() < c_large_tag; }

    [[nodiscard]] friend constexpr bool operator==(const ConstString& lhs, const ConstString& rhs) {
        return lhs.view() == rhs.view();
    }
    [[nodiscard]] friend constexpr auto operator<=>(const ConstString& lhs, const ConstString& rhs) {
        return lhs.view() <=> rhs.view();
    }
    [[nodiscard]] friend constexpr bool operator==(std::string_view lhs, const ConstString& rhs) {
        return lhs == rhs.view();
    }
    [[nodiscard]] friend constexpr auto operator<=>(std::string_view lhs, const ConstString& rhs) {
        return lhs <=> rhs.view();
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr explicit ConstString(bool allowShort, std::string_view n) {
        if (allowShort && n.size() <= c_max_small) {
            std::copy_n(n.data(), n.size(), storage_);
            storage_[n.size()]    = 0;
            storage_[c_max_small] = static_cast<char>(c_max_small - n.size());
        }
        else {
            initLarge(n);
        }
    }
    static constexpr auto c_max_small  = 15u;
    static constexpr auto c_large_tag  = c_max_small + 1u;
    static constexpr auto c_borrow_tag = c_large_tag + 1u;
    static constexpr auto pad_size     = static_cast<uint32_t>(c_large_tag - (sizeof(const char*) + sizeof(uint32_t)));
    constexpr void        reset() {
        storage_[0]           = 0;
        storage_[c_max_small] = static_cast<char>(c_max_small);
    }
    constexpr void moveFrom(ConstString&& o) noexcept {
        if (o.small()) {
            std::copy_n(o.storage_, sizeof(storage_), storage_);
        }
        else {
            std::memcpy(storage_, o.storage_, sizeof(storage_));
        }
        o.reset();
    }
    struct Large {
        [[nodiscard]] constexpr auto view() const -> std::string_view { return {str, size}; }
        const char*                  str;
        uint32_t                     size;
        char                         pad[pad_size];
    };
    [[nodiscard]] constexpr auto tag() const -> uint8_t { return static_cast<uint8_t>(storage_[c_max_small]); }
    [[nodiscard]] auto           large() const -> const Large* { return reinterpret_cast<const Large*>(storage_); }
    void                         initLarge(std::string_view str);
    static void                  release(const char*);
    constexpr void               release() {
        if (tag() == c_large_tag) {
            ConstString::release(large()->str);
        }
    }

    alignas(const char*) char storage_[c_max_small + 1];
};
static_assert(sizeof(ConstString) == 16u);
static_assert(ConstString{}.small());
static_assert(ConstString{}.size() == 0u);
static_assert(ConstString{ConstString{}}.small());
static_assert(ConstString{"small"} == "small");
static_assert(ConstString{"small"}.size() == 5u);

template <typename MapType, typename... Args>
auto try_emplace(MapType& map, std::string_view key, Args&&... args)
    -> decltype(map.try_emplace(std::declval<ConstString>(), std::forward<Args>(args)...)) {
    // Create a "borrowed" key for lookup. If `key` is not in map, `try_emplace()` will copy-construct a key from `k`,
    // thereby materializing a full string. Otoh, if `map` already contains an element with the given key, we avoid an
    // unnecessary allocation.
    // NOTE: Once we have C++26 and the heterogeneous version of `try_emplace()`, this workaround can be removed.
    ConstString k{ConstString::Borrow_t{}, key};
    return map.try_emplace(k, std::forward<Args>(args)...);
}

//! A simple string hash set that preserves the insertion order.
class OrderedStringSet {
public:
    using trivially_relocatable = std::true_type; // NOLINT
    OrderedStringSet()          = default;
    explicit OrderedStringSet(bool allowShort);
    OrderedStringSet(const OrderedStringSet&)            = delete;
    OrderedStringSet(OrderedStringSet&&) noexcept        = default;
    OrderedStringSet& operator=(const OrderedStringSet&) = delete;
    OrderedStringSet& operator=(OrderedStringSet&&)      = default;
    ~OrderedStringSet();

    //! Returns the number of elements in the set.
    [[nodiscard]] auto size() const -> uint32_t { return static_cast<uint32_t>(strings_.size() / sizeof(ConstString)); }
    //! Returns a view over the current elements.
    /*!
     * \note The view is only valid until the next call to a mutating function.
     */
    [[nodiscard]] auto elements() const noexcept -> std::span<const ConstString> {
        return {reinterpret_cast<const ConstString*>(strings_.data()), size()};
    }
    //! Returns a (const) reference to the ith element in the set.
    /*!
     * \note The reference is only valid until the next call to a mutating function.
     */
    [[nodiscard]] auto operator[](Id_t i) const -> const ConstString& {
        assert(i < size());
        return elements()[i];
    }
    //! Checks if the set contains the given string.
    [[nodiscard]] bool contains(std::string_view str) const noexcept {
        return index_.find_if(hashStr(str), [&](Id_t idx) { return (*this)[idx] == str; }).valid();
    }

    //! Adds the give string to the set if it is not yet in the set.
    /*!
     * \param str The string to add.
     * \return A pair containing the index of the string in the set and a boolean indicating whether it had to be added.
     */
    auto add(std::string_view str) -> std::pair<Id_t, bool> {
        auto hash = hashStr(str);
        auto r    = index_.find_if(hash, [&](Id_t idx) { return (*this)[idx] == str; });
        if (r) {
            return {*r, false};
        }
        auto id = size();
        push(str);
        index_.add(r, hash, id);
        return {id, true};
    }

    //! Removes all elements from the set.
    void clear();

private:
    [[nodiscard]] static auto hashStr(std::string_view str) -> uint32_t {
        return static_cast<uint32_t>(std::hash<std::string_view>{}(str));
    }
    void push(std::string_view);

    DynamicIndex  index_;
    DynamicBuffer strings_;
    bool          allowShort_{true};
};

namespace Detail {
template <typename T>
struct Temp {
    Temp() = default;
    ~Temp() {
        if constexpr (not std::is_trivially_destructible_v<T>) {
            std::destroy_n(data(), size());
        }
    }
    void resize(std::size_t n, const T& v) {
        Temp t;
        std::uninitialized_fill_n(reinterpret_cast<T*>(t.buffer.alloc(n * sizeof(T)).data()), n, v);
        t.buffer.swap(buffer);
    }
    auto               data() const -> T* { return reinterpret_cast<T*>(buffer.data()); }
    [[nodiscard]] auto size() const -> std::size_t { return buffer.size() / sizeof(T); }
    DynamicBuffer      buffer;
};
} // namespace Detail

struct RadixConfig {
    static constexpr auto def_threshold    = 32u;
    std::size_t           stdSortThreshold = 0u;   //!< Threshold for std sort or 0u for default.
    bool                  stdStable        = true; //!< Whether std sort must be stable.
};
constexpr inline auto radix_def     = RadixConfig{};
constexpr inline auto radix_relaxed = RadixConfig{.stdSortThreshold = 0u, .stdStable = false};
constexpr inline auto radix_only    = RadixConfig{.stdSortThreshold = 1u, .stdStable = true};

//! Stable LSD radix sort for contiguous ranges using an unsigned rank key.
/*!
 * Sorts the elements in `rng` by the key returned from `rank` using radix sort.
 *
 * \tparam R      A \c std::ranges::contiguous_range;
 * \tparam RankFn Callable returning an unsigned integral rank for elements of `rng`.
 * \tparam Tb     Temporary buffer provider; must provide \c resize(std::size_t) and \c data()\->T\*.
 *
 * \param rng    The range to be sorted in place.
 * \param rank   Projection returning the unsigned rank key for an element.
 * \param config Threshold below which std sort is used and whether the operation must be stable.
 * \param tmp    Temporary buffer used for redistribution; allocated lazily on the first necessary pass.
 *
 * \note The rank function is evaluated multiple times per element and pass; it should be fast and free of side effects.
 * \note If the size of the input range is smaller than the configured threshold, the function falls back to
 *       std::ranges::sort or std::ranges::stable_sort depending on whether the output must be stable.
 * \note Use std::ref(buffer) to reuse an existing temporary buffer.
 */
template <std::ranges::contiguous_range R, typename RankFn, typename Tb = Detail::Temp<std::ranges::range_value_t<R>>>
requires std::is_invocable_v<RankFn&, std::ranges::range_value_t<R>> &&
         std::is_unsigned_v<std::remove_cvref_t<std::invoke_result_t<RankFn&, std::ranges::range_value_t<R>>>>
constexpr void radixSort(R&& rng, RankFn rank, RadixConfig config = radix_def, Tb tmp = {}) {
    using T        = std::ranges::range_value_t<R>;
    using Rank     = std::remove_cvref_t<std::invoke_result_t<RankFn&, T>>;
    using SizeType = std::common_type_t<std::size_t, Rank>;
    assert(std::size(rng) <= UINT32_MAX);
    const auto n = static_cast<uint32_t>(std::size(rng));
    if (n < 2) {
        return;
    }
    if (n < config.stdSortThreshold || (config.stdSortThreshold == 0u && n < RadixConfig::def_threshold)) {
        if (not config.stdStable) {
            std::ranges::sort(std::forward<R>(rng), [&](const T& a, const T& b) { return rank(a) < rank(b); });
        }
        else {
            std::ranges::stable_sort(std::forward<R>(rng), [&](const T& a, const T& b) { return rank(a) < rank(b); });
        }
        return;
    }
    // Config
    static_assert(CHAR_BIT == 8);
    constexpr auto base   = uint32_t{256};
    constexpr auto bits   = uint32_t{8};
    constexpr auto mask   = base - 1;
    constexpr auto passes = static_cast<uint32_t>(sizeof(Rank));
    constexpr auto digit  = [](auto val, uint32_t shift) { return static_cast<uint32_t>((val >> shift) & mask); };
    // Data
    uint32_t count[base];
    auto     bm = SizeType{mask}, lb = static_cast<SizeType>(-1), ub = SizeType{0};
    auto*    src  = std::data(rng);
    auto*    dest = static_cast<T*>(nullptr);
    // 1. Count digit occurrences and optionally compute bit envelope
    auto countDigits = [&]<bool B>(std::integral_constant<bool, B>, uint32_t shift) {
        auto sorted = uint32_t{0};
        auto prev   = uint32_t{0};
        for (uint32_t i = 0u; i < n; ++i) {
            const auto r = rank(src[i]);
            const auto d = digit(r, shift);
            ++count[d];
            sorted += (d >= prev);
            prev    = d;
            if constexpr (B) { // compute bit envelope
                lb &= r;
                ub |= r;
            }
        }
        return sorted == n;
    };
    std::fill_n(count, base, 0);
    auto sorted = countDigits(std::true_type{}, 0u);
    for (uint32_t pass = 0u, minD = 0u, maxD = mask; pass < passes; ++pass, bm <<= bits) {
        if ((lb & bm) == (ub & bm)) {
            continue;
        }
        const auto shift = pass * bits;
        if (pass > 0) {
            std::fill_n(count + minD, maxD - minD + 1, 0);
            sorted = countDigits(std::false_type{}, shift);
        }
        minD = digit(lb, shift);
        maxD = digit(ub, shift);

        if (sorted) {
            continue;
        }
        // 2. Compute starting indices
        for (auto i = minD, pos = uint32_t{0}; i <= maxD; ++i) { pos += std::exchange(count[i], pos); }

        // 3. Redistribute
        if (not dest) {
            if constexpr (std::is_same_v<Tb, std::unwrap_reference_t<Tb>>) {
                tmp.resize(n, *src);
                dest = tmp.data();
            }
            else {
                tmp.get().resize(n, *src);
                dest = tmp.get().data();
            }
        }
        for (uint32_t i = 0; i < n; ++i) {
            const auto d = digit(rank(src[i]), shift);
            const auto p = count[d]++;
            assert(p < n);
            dest[p] = std::move(src[i]);
        }

        // Swap source and destination for the next pass
        std::swap(src, dest);
    }
    if (src != std::data(rng)) {
        // Move result back to input
        std::ranges::move(src, src + n, std::data(rng));
    }
}

///@}

} // namespace Potassco
template <>
struct std::hash<Potassco::ConstString> : std::hash<std::string_view> {
    using is_transparent = void; // NOLINT
    using std::hash<std::string_view>::operator();
    auto operator()(const Potassco::ConstString& str) const noexcept -> std::size_t { return (*this)(str.view()); }
};
