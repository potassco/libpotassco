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
#include <unordered_map>
#include <utility>

namespace Potassco {
namespace Detail {
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
            grow(n);
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
        reserve(size() + 1);
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
    void grow(std::size_t n);

    void*    beg_{nullptr};
    uint32_t cap_{0};
    uint32_t sizeOwn_{0};
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

//! Enumeration type for guiding hash probe lookup.
enum class HashProbeResult {
    success,  //!< Element found - stop probing.
    fail,     //!< Empty element found - stop probing.
    removed,  //!< Probe hit a removed element, which might be returned if no other element is found.
    collision //!< Probe hit a valid element, but probing should continue.
};
//! A (dynamically sized) array type intended to be used as a foundation for linear probing hash tables.
template <typename T, std::unsigned_integral SizeT = std::size_t>
class DynamicHashArray {
public:
    using pointer   = T*;    // NOLINT
    using size_type = SizeT; // NOLINT

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

    //! Returns the array's current capacity.
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
        return static_cast<size_type>(std::size(arr_));
    }
    //! Returns the number of remaining elements assuming the given max load factor and "in-use" elements.
    [[nodiscard]] constexpr auto avail(size_type used, double lf) const noexcept -> size_type {
        return static_cast<size_type>(capacity() * lf) - used;
    }
    //! Returns a pointer to the internal array.
    [[nodiscard]] constexpr auto data() const noexcept -> pointer { return arr_.data(); }
    //! Returns the array's current hash mask.
    [[nodiscard]] constexpr auto mask() const noexcept -> size_type { return capacity() - 1; }
    //! Returns the element at the given array index.
    [[nodiscard]] constexpr auto operator[](size_type i) -> T& { return arr_[i]; }

    //! Returns the first position in the array that could store an element with the given hash.
    /*!
     * \pre capacity() > 0
     * \param hash The hash to lookup.
     * \param pred The search predicate to apply on each visited element.
     * \note Search is stopped once the given predicate returns a "terminating" probe result, i.e.,
     *       HashProbeResult::success, or HashProbeResult::fail.
     * \return The position where an entry with the given hash should be stored according to the provided predicate.
     */
    template <std::unsigned_integral HashT, typename Pred>
    requires(std::is_invocable_r_v<HashProbeResult, Pred, T>)
    [[nodiscard]] auto lookup(HashT hash, const Pred& pred) const -> pointer {
        assert(capacity());
        const auto m = mask();
        for (pointer pos = nullptr, data = arr_.data();; ++hash) {
            auto b = hash & m;
            if (auto r = pred(data[b]); r != HashProbeResult::collision) {
                if (not pos || r == HashProbeResult::success) {
                    pos = &data[b];
                }
                if (r != HashProbeResult::removed) {
                    return pos;
                }
            }
        }
    }
    //! Doubles the capacity of this array and relocates all "relevant" elements.
    /*!
     * \param hasher The hash function to apply, which shall return the hash for a given entry.
     * \param pred   The "filter" predicate used to determine "relevant" elements.
     * \note The given predicate shall return true for "relevant" and false for empty/removed entries.
     * \return The number of "relevant" elements in the array.
     */
    template <typename H, typename Pred>
    requires(std::is_invocable_r_v<bool, Pred, T>)
    auto grow(const H& hasher, const Pred& pred) -> size_type {
        auto       tmp  = DynamicHashArray(std::max(capacity() * 2u, static_cast<size_type>(1u)));
        auto       used = static_cast<size_type>(0);
        const auto m    = tmp.mask();
        for (auto& b : arr_) {
            if (pred(b)) {
                ++used;
                for (auto k = hasher(b);; ++k) {
                    if (auto& p = tmp[k & m]; not pred(p)) {
                        p = std::move(b);
                        break;
                    }
                }
            }
        }
        std::swap(arr_, tmp.arr_);
        return used;
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
        [[nodiscard]] constexpr auto used() const noexcept { return value < id_tomb; }

        uint32_t hash{0u};
        Id_t     value{id_empty};
    };

public:
    using HashType = Id_t;

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
        if (empty()) {
            return {};
        }
        return IndexRef{table_.lookup(hash, [&](const Bucket& e) {
            if (not e.used()) {
                return e.value == id_empty ? HashProbeResult::fail : HashProbeResult::removed;
            }
            return e.hash == hash && func(e.value) ? HashProbeResult::success : HashProbeResult::collision;
        })};
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
        auto* bucket  = pos.pos_ != nullptr ? const_cast<Bucket*>(pos.pos_) : next(hash);
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
    [[nodiscard]] auto next(HashType hash) noexcept -> Bucket* {
        for (const auto& m = table_.mask();; ++hash) {
            if (auto pos = hash & m; not table_[pos].used()) {
                return &table_[pos];
            }
        }
    }
    void grow();
    using BucketArray = DynamicHashArray<Bucket, uint32_t>;
    BucketArray table_;
    uint32_t    size_{0u};
    uint32_t    grow_{0u};
    uint32_t    tombs_{0u};
    uint32_t    lf_{8500u};
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
    [[nodiscard]] constexpr auto c_str() const -> const char* { return small() ? storage_ : large()->str; }
    //! Converts this string to a string_view.
    [[nodiscard]] constexpr auto view() const -> std::string_view { return static_cast<std::string_view>(*this); }
    //! Returns the length of this string.
    [[nodiscard]] constexpr auto size() const -> std::size_t { return small() ? c_max_small - tag() : large()->size; }
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
    void                         init(std::string_view str);
    [[nodiscard]] auto           large() const -> const Large* { return reinterpret_cast<const Large*>(storage_); }
    [[nodiscard]] constexpr auto tag() const -> uint8_t { return static_cast<uint8_t>(storage_[c_max_small]); }
    void                         release();
    constexpr void               reset() {
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
