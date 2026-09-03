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

namespace Potassco {

template <std::integral To, std::integral From>
constexpr To safe_cast(From from) {
    POTASSCO_CHECK(std::in_range<To>(from), Errc::out_of_range);
    return static_cast<To>(from);
}
template <std::integral To, typename From>
requires(std::is_enum_v<From>)
constexpr To safe_cast(From from) {
    return safe_cast<To>(to_underlying(from));
}
template <std::integral To = uint32_t, typename C>
constexpr To size_cast(const C& c) {
    static_assert(std::is_unsigned_v<decltype(c.size())>, "unsigned size expected");
    if constexpr (std::is_unsigned_v<To> && sizeof(To) >= sizeof(decltype(c.size()))) {
        return static_cast<To>(c.size());
    }
    else {
        return safe_cast<To>(c.size());
    }
}

//! Type trait checking whether a given type is trivially-relocatable (i.e. "bitwise-movable").
/*!
 * By default, only trivially copyable types are considered trivially-relocatable. All other types require
 * explicit "opt-in". To "opt-in", a type either has to define a typedef called `trivially_relocatable` that must alias
 * to a boolean constant type (e.g. std::true_type), or specialize Potassco::is_trivially_relocatable.
 */
template <typename T> // clang-format off
struct is_trivially_relocatable // NOLINT
    : std::bool_constant < std::is_trivially_copyable_v<T> ||
    requires {
    requires T::trivially_relocatable::value;
} > {};
// clang-format on
template <typename T, typename U>
struct is_trivially_relocatable<std::pair<T, U>>
    : std::integral_constant<bool, is_trivially_relocatable<T>::value && is_trivially_relocatable<U>::value> {};

template <typename T>
struct is_trivially_relocatable<std::unique_ptr<T>> : std::true_type {};

template <typename T>
constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;

template <typename T>
concept TriviallyRelocatable = is_trivially_relocatable_v<T>;

//! Convenience macro for marking a type as trivially-relocatable.
#define POTASSCO_TRIVIALLY_RELOCATABLE(...)                                                                            \
    using trivially_relocatable = std::bool_constant<[](auto... args) { return (... && args); }(__VA_ARGS__)>

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
            x      ^= x >> 31u;
            return static_cast<uint32_t>(x);
        }
    }
};
auto dupString(std::string_view in) -> char*;
template <TriviallyRelocatable T>
void destructiveMove(T* target, T* source) {
    if constexpr (std::is_trivial_v<T>) {
        *target = *source;
    }
    else {
        std::memcpy(static_cast<void*>(target), static_cast<void*>(source), sizeof(T));
    }
}
template <typename It, typename OutT>
constexpr void uninitialized_copy_n(It first, std::size_t n, OutT* out) {
    using RefT = decltype(*first);
    using ValT = std::remove_cvref_t<RefT>;
    if (n) [[likely]] {
        if constexpr (std::is_trivially_constructible_v<OutT, RefT> && std::contiguous_iterator<It> &&
                      sizeof(OutT) == sizeof(ValT) &&
                      (std::is_same_v<OutT, ValT> || (std::is_integral_v<OutT> && std::is_integral_v<ValT>) )) {
            std::memcpy(static_cast<void*>(out), static_cast<const void*>(std::to_address(first)), n * sizeof(OutT));
        }
        else {
            std::uninitialized_copy_n(first, n, out);
        }
    }
}
static constexpr auto fastGrowSize(std::size_t valSize) -> std::size_t { return 0x20000u / valSize; }
template <typename SizeT>
static constexpr auto goodNextSize(SizeT minS, SizeT cap, SizeT valSize) -> SizeT {
    if (cap == 0u) {
        cap = std::max(static_cast<SizeT>(SystemAllocator::realloc_max_align), valSize) / valSize;
    }
    else if (cap > 8u && cap <= static_cast<SizeT>(fastGrowSize(valSize))) {
        cap = (cap * 3 + 1) / 2u;
    }
    else {
        cap = cap + cap;
    }
    auto ns = std::max(cap, minS);
    auto gs = static_cast<SizeT>(SystemAllocator::goodAllocSize(ns * valSize)) / valSize;
    return gs > minS && gs <= (UINT32_MAX / valSize) ? gs : minS;
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

//! A (dynamically sized) array of T.
/*!
 * The class is similar to std::vector<T>, but with reduced API and fewer guarantees.
 * Major caveats:
 * - DynamicArray only supports trivially-relocatable (i.e. "bitwise-movable") types (checked at compile-time).
 * - Push/append/insert operations **must not** reference elements in the array (precondition - not checked).
 * - There is no support for bulk insertions at arbitrary positions.
 * - Mutating operations only provide basic exception safety.
 * - There is no fine-grained allocator support - all allocations happen through the system allocator.
 */
template <TriviallyRelocatable T>
requires(alignof(T) <= Potassco::SystemAllocator::realloc_max_align)
class DynamicArray {
public:
    POTASSCO_TRIVIALLY_RELOCATABLE();
    // NOLINTBEGIN
    using size_type                = uint32_t;
    using pointer                  = T*;
    using const_pointer            = const T*;
    using iterator                 = pointer;
    using const_iterator           = const_pointer;
    using reverse_iterator         = std::reverse_iterator<iterator>;
    using const_reverse_iterator   = std::reverse_iterator<const_iterator>;
    using reference                = T&;
    using const_reference          = const T&;
    using value_type               = T;
    using value_type_param         = Detail::Param_t<value_type>;
    static constexpr auto val_size = static_cast<size_type>(sizeof(T));
    // NOLINTEND

    //! Creates an empty array.
    constexpr DynamicArray() = default;
    //! Creates an array with `n` default constructed objects.
    constexpr explicit DynamicArray(size_type n) : DynamicArray(std::piecewise_construct, n) {}
    //! Creates an array with `n` copies of `val`.
    DynamicArray(size_type n, value_type_param val) : DynamicArray(std::piecewise_construct, n, val) {}
    //! Creates a copy of other.
    DynamicArray(const DynamicArray& other) : DynamicArray(other.begin(), other.end()) {}
    //! Steals the contents from other.
    constexpr DynamicArray(DynamicArray&& other) noexcept
        : data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0u))
        , cap_(std::exchange(other.cap_, 0u)) {}
    //! Creates an array with copies of the elements in the range [first, last).
    template <std::forward_iterator It>
    DynamicArray(It first, It last) : DynamicArray(std::piecewise_construct, checkRange(first, last), first) {}
    //! Creates an array with copies of the elements in `x`.
    DynamicArray(std::initializer_list<value_type> x) : DynamicArray(x.begin(), x.end()) {}

    //! Destroys the array and its contents.
    constexpr ~DynamicArray() {
        destroy(data(), size());
        if (not std::is_constant_evaluated()) {
            deallocate();
        }
    }

    //! Replaces the contents of this array with copies of the values in `other`.
    auto operator=(const DynamicArray& other) -> DynamicArray& {
        if (this != &other) {
            assign(other.begin(), other.end());
        }
        return *this;
    }
    //! Replaces the contents of this array with copies of the values in `x`.
    auto operator=(std::initializer_list<value_type> x) -> DynamicArray& {
        assign(x.begin(), x.end());
        return *this;
    }
    //! Replaces the contents of this array with the contents of `other`.
    constexpr auto operator=(DynamicArray&& other) noexcept -> DynamicArray& {
        if (this != &other) {
            destroy(data(), size());
            deallocate();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0u);
            cap_  = std::exchange(other.cap_, 0u);
        }
        return *this;
    }
    //! Replaces the contents of this array with `n` copies of `val`.
    void assign(size_type n, value_type_param val) {
        clear();
        append(n, val);
    }
    //! Replaces the contents of this array with copies of the values in the range [first, last).
    template <std::forward_iterator It>
    void assign(It first, It last) {
        auto n = checkRange(first, last);
        clear();
        append(n, first);
    }
    //! Replaces the contents of this array with copies of the values in `x`.
    void assign(std::initializer_list<value_type> x) { assign(x.begin(), x.end()); }

    //! Returns a pointer to the beginning of the underlying array.
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return data_; }
    //! \copydoc data()
    [[nodiscard]] constexpr auto data() noexcept -> pointer { return data_; }
    //! \name Iterators
    //!@{
    //! Returns an iterator to the first element of the array.
    [[nodiscard]] constexpr auto begin() -> iterator { return data(); }
    //! \copydoc begin()
    [[nodiscard]] constexpr auto begin() const -> const_iterator { return data(); }
    //! \copydoc begin()
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return data(); }
    //! Returns an iterator to the element following the last element of the array.
    [[nodiscard]] constexpr auto end() -> iterator { return begin() + size(); }
    //! \copydoc end()
    [[nodiscard]] constexpr auto end() const -> const_iterator { return begin() + size(); }
    //! \copydoc end()
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return cbegin() + size(); }
    //! Returns a reverse iterator to the first element of the reversed array.
    [[nodiscard]] constexpr auto rbegin() noexcept -> reverse_iterator { return std::make_reverse_iterator(end()); }
    //! \copydoc rbegin()
    [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator {
        return std::make_reverse_iterator(end());
    }
    //! \copydoc rbegin()
    [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator {
        return std::make_reverse_iterator(end());
    }
    //! Returns a reverse iterator to the element following the last element of the reversed array.
    [[nodiscard]] constexpr auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    //! \copydoc rend()
    [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator {
        return std::make_reverse_iterator(begin());
    }
    //! \copydoc rend()
    [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator {
        return std::make_reverse_iterator(begin());
    }
    //!@}

    //! Returns a reference to the element at the given position.
    /*!
     * \pre pos < size()
     */
    [[nodiscard]] constexpr auto operator[](size_type pos) const -> const_reference {
        return const_cast<DynamicArray&>(*this)[pos];
    }
    //! \copydoc const_reference operator[](size_type) const
    [[nodiscard]] constexpr auto operator[](size_type pos) -> reference {
        POTASSCO_DEBUG_ASSERT(pos < size());
        return data_[pos];
    }
    //! Returns a reference to the element at the given position.
    /*!
     * \throw std::out_of_range if pos >= size()
     */
    [[nodiscard]] constexpr auto at(size_type pos) const -> const_reference {
        return const_cast<DynamicArray&>(*this).at(pos);
    }
    //! \copydoc at(size_type)
    [[nodiscard]] constexpr auto at(size_type pos) -> reference {
        POTASSCO_CHECK(pos < size(), Errc::out_of_range, "DynamicArray::at()");
        return data_[pos];
    }
    //! Returns a reference to the first element of the array.
    /*!
     * \pre not empty()
     */
    [[nodiscard]] constexpr auto front() const -> const_reference { return this->operator[](0); }
    //! \copydoc front()
    [[nodiscard]] constexpr auto front() -> reference { return this->operator[](0); }
    //! Returns a reference to the last element of the array.
    /*!
     * \pre not empty()
     */
    [[nodiscard]] constexpr auto back() const -> const_reference { return this->operator[](size() - 1); }
    //! \copydoc back()
    [[nodiscard]] constexpr auto back() -> reference { return this->operator[](size() - 1); }

    //! Returns whether the array is empty.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size() == 0u; }
    //! Returns the number of elements in the array.
    [[nodiscard]] constexpr auto size() const noexcept -> size_type { return size_; }
    //! Returns the current capacity of the array.
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type { return cap_; }
    //! Returns the maximum number of elements this array can hold.
    [[nodiscard]] constexpr auto max_size() const noexcept -> size_type { return UINT32_MAX / val_size; }

    //! Appends `u` to the end of the array.
    /*!
     * \pre `u` must not reference an element in this array.
     */
    void push_back(value_type_param u) {
        if (auto sz = size(); sz != capacity()) {
            std::construct_at(data_ + sz, u);
        }
        else {
            std::construct_at(reallocNext(sz, 1u), u);
        }
        ++size_;
    }
    //! \copydoc push_back(value_type_param)
    void push_back(value_type&& u) requires(not std::is_same_v<value_type_param, T>)
    {
        emplace_back(std::move(u));
    }
    //! Constructs a new element at the end of the array.
    /*!
     * \pre `args...` must not reference elements in this array.
     * \return A reference to the inserted element.
     */
    template <typename... Args>
    auto emplace_back(Args&&... args) -> reference {
        if (auto sz = size(); sz != capacity()) {
            std::construct_at(data_ + sz, std::forward<Args>(args)...);
        }
        else {
            std::construct_at(reallocNext(sz, 1u), std::forward<Args>(args)...);
        }
        return data_[size_++];
    }

    //! Constructs a new element right before `pos`.
    /*!
     * \pre `pos` must be a valid iterator of this array and `args...` must not reference elements in this array.
     * \return An iterator pointing to the inserted element.
     */
    template <typename... Args>
    auto emplace(const_iterator pos, Args&&... args) -> iterator {
        POTASSCO_DEBUG_ASSERT(pos >= begin() && pos <= end());
        auto idx  = static_cast<size_type>(pos - begin());
        auto tail = size() - idx;
        if (tail == 0u) {
            return std::addressof(emplace_back(std::forward<Args>(args)...));
        }
        prepare(1u);
        auto insPos = data_ + idx;
        // shift tail one position to the right
        std::memmove(static_cast<void*>(insPos + 1), static_cast<void*>(insPos), valSize(tail));
        if constexpr (std::is_nothrow_constructible_v<value_type, Args...>) {
            std::construct_at(insPos, std::forward<Args>(args)...);
        }
        else {
            try {
                std::construct_at(insPos, std::forward<Args>(args)...);
            }
            catch (...) {
                // rollback to previous state
                std::memmove(static_cast<void*>(insPos), static_cast<void*>(insPos + 1), valSize(tail));
                throw;
            }
        }
        ++size_;
        return insPos;
    }

    //! Inserts `u` right before `pos`.
    /*!
     * \pre `pos` must be a valid iterator of this array and `u` must not reference an element in this array.
     * \return An iterator pointing to the inserted element.
     */
    auto insert(const_iterator pos, value_type_param u) -> iterator { return emplace(pos, u); }
    //! \copydoc insert(const_iterator, value_type_param)
    auto insert(const_iterator pos, value_type&& u) -> iterator requires(not std::is_same_v<value_type_param, T>)
    {
        return emplace(pos, std::move(u));
    }

    //! Appends copies of the elements in the range [first, last).
    /*!
     * \pre The source range must not overlap this array's storage.
     */
    template <std::forward_iterator It>
    void append(It first, It last) {
        append(checkRange(first, last), first);
    }
    //! Appends the elements in `x`.
    void append(std::initializer_list<T> x) { append(x.begin(), x.end()); }
    //! Appends `n` copies of `val`.
    /*!
     * \pre `val` must not reference an element in this array.
     */
    void append(size_type n, value_type_param val) {
        std::uninitialized_fill_n(prepare(n), n, val);
        size_ += n;
    }
    //! Appends `n` value-initialized elements.
    void append(size_type n) {
        std::uninitialized_value_construct_n(prepare(n), n);
        size_ += n;
    }
    //! Extends the array by `n` elements and returns writable storage for the appended tail.
    /*!
     * The returned span refers to elements that are already part of the array.
     * Callers are responsible for initializing/overwriting all returned elements before they are read or destroyed.
     */
    auto appendForOverwrite(size_type n) -> std::span<T> {
        prepare(n);
        return {data_ + std::exchange(size_, size_ + n), n};
    }

    //! Removes the last element of this array.
    /*!
     * \pre not empty().
     */
    void pop_back() {
        POTASSCO_DEBUG_ASSERT(not empty());
        destroy(end() - 1);
        --size_;
    }
    //! Removes the last `n` elements of this array.
    /*!
     * \pre `n <= size()`.
     */
    void pop(size_type n) {
        if (n) {
            POTASSCO_DEBUG_ASSERT(n <= size());
            destroy(end() - n, n);
            size_ -= n;
        }
    }

    //! Erases the element at `pos`.
    /*!
     * \pre `pos` is a valid iterator in `[begin(), end())`.
     * \return Iterator to the element following the erased one, or `end()` if no such element exists.
     */
    constexpr auto erase(const_iterator pos) -> iterator {
        POTASSCO_DEBUG_ASSERT(pos >= begin() && pos < end());
        auto p    = const_cast<pointer>(pos);
        auto next = p + 1;
        destroy(p);
        if (auto tail = static_cast<size_type>(end() - next); tail) {
            std::memmove(static_cast<void*>(p), static_cast<void*>(next), valSize(tail));
        }
        --size_;
        return p;
    }

    //! Erases all elements satisfying `pred`.
    /*!
     * \return The number of erased elements.
     */
    template <std::predicate<value_type_param> Pred>
    auto erase_if(Pred pred) -> size_type {
        if (auto last = end(), first = std::find_if(data_, last, std::ref(pred)); first != last) {
            destroy(first);
            auto out = first++;
            for (; first != last; ++first) {
                if (not pred(*first)) {
                    Detail::destructiveMove(out++, first);
                }
                else {
                    destroy(first);
                }
            }
            auto r  = static_cast<size_type>(last - out);
            size_  -= r;
            return r;
        }
        return 0u;
    }

    //! Ensures that capacity is at least `nc` without changing the size of this array.
    /*!
     * \note The function never decreases the capacity of the array.
     */
    void reserve(size_type nc) {
        if (nc > capacity()) {
            realloc(static_cast<size_type>(SystemAllocator::goodAllocSize(valSize(nc))) / val_size);
        }
    }

    //! Resizes the array to `count`.
    /*!
     * Appends copies of `val` if `count > size()`, otherwise removes trailing elements.
     */
    void resize(size_type count, value_type_param val) {
        if (auto sz = size(); count > sz) {
            append(count - sz, val);
        }
        else {
            pop(sz - count);
        }
        POTASSCO_DEBUG_ASSERT(size() == count);
    }
    //! Resizes the array to `count`.
    /*!
     * Appends value-initialized elements if `count > size()`, otherwise removes trailing elements.
     */
    void resize(size_type count) {
        if (auto sz = size(); count > sz) {
            append(count - sz);
        }
        else {
            pop(sz - count);
        }
        POTASSCO_DEBUG_ASSERT(size() == count);
    }

    //! Removes all elements from the array without changing the array's capacity.
    void clear() { destroy(data(), std::exchange(size_, 0u)); }

    //! Swaps `this` and `other`.
    void swap(DynamicArray& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(cap_, other.cap_);
    }

    //! Resets the array to its default-constructed state by removing all elements and realising any allocated storage.
    void reset() {
        destroy(data(), size());
        deallocate();
        data_ = nullptr;
        size_ = cap_ = 0u;
    }

    //! Requests capacity reduction to fit current size.
    /*!
     * If shrinking allocation fails, the array remains unchanged.
     */
    void shrink_to_fit() {
        if (auto rc = not empty() ? static_cast<size_type>(SystemAllocator::goodAllocSize(valSize(size()))) : 0u;
            rc < valSize(capacity())) {
            void* newMem = nullptr;
            if (rc) {
                newMem = SystemAllocator::allocate(rc);
                std::memcpy(newMem, static_cast<void*>(data_), valSize(size()));
            }
            deallocate();
            data_ = static_cast<T*>(newMem);
            cap_  = rc / val_size;
        }
    }

private:
    [[nodiscard]] static constexpr auto valSize(size_type n) noexcept -> size_type {
        return static_cast<std::size_t>(n * val_size);
    }
    template <std::forward_iterator It>
    [[nodiscard]] constexpr auto checkRange(It first, It last) const -> size_type {
        auto diff = static_cast<std::size_t>(std::distance(first, last));
        POTASSCO_CHECK(std::cmp_less_equal(diff, max_size()), Errc::length_error, "DynamicArray::appendRange");
        return static_cast<size_type>(diff);
    }
    void deallocate() { SystemAllocator::deallocate(data_, valSize(cap_)); }
    void realloc(size_type n) {
        data_ = static_cast<T*>(SystemAllocator::reallocate(static_cast<void*>(data_), valSize(n)));
        cap_  = n;
    }
    auto reallocNext(size_type sz, size_type n) -> pointer {
        POTASSCO_CHECK(max_size() - sz >= n, Errc::length_error, "DynamicArray::push");
        auto minS = sz + n;
        realloc(Detail::goodNextSize(minS, capacity(), val_size));
        return data_ + sz;
    }
    template <typename... Arg>
    requires(sizeof...(Arg) <= 1)
    DynamicArray(std::piecewise_construct_t, size_type n, Arg&&... arg) {
        reserve(n);
        append(n, std::forward<Arg>(arg)...);
    }
    POTASSCO_ATTR_INLINE auto prepare(size_type n) -> pointer {
        return capacity() - size() >= n ? data_ + size() : reallocNext(size(), n);
    }
    template <std::forward_iterator It>
    POTASSCO_ATTR_INLINE void append(size_type n, It first) {
        if (n) {
            Detail::uninitialized_copy_n(first, n, prepare(n));
            size_ += n;
        }
    }
    POTASSCO_ATTR_INLINE constexpr void destroy(pointer first) {
        if constexpr (not std::is_trivially_destructible_v<T>) {
            std::destroy_at(first);
        }
    }
    POTASSCO_ATTR_INLINE constexpr void destroy(pointer first, size_type n) {
        if constexpr (not std::is_trivially_destructible_v<T>) {
            std::destroy_n(first, n);
        }
    }
    pointer   data_{nullptr};
    size_type size_{0};
    size_type cap_{0};
};
template <std::forward_iterator It>
DynamicArray(It, It) -> DynamicArray<typename std::iterator_traits<It>::value_type>;

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

template <typename T, typename V>
constexpr auto erase(DynamicArray<T>& vec, const V& value) -> typename DynamicArray<T>::size_type {
    return vec.erase_if([&](typename DynamicArray<T>::value_type_param x) { return x == value; });
}
template <typename T, typename P>
constexpr auto erase_if(DynamicArray<T>& vec, P pred) -> typename DynamicArray<T>::size_type {
    return vec.erase_if(std::ref(pred));
}

class DynamicBitset {
public:
    using IndexType = uint32_t;
    POTASSCO_TRIVIALLY_RELOCATABLE();

    //! Creates an empty set.
    DynamicBitset() noexcept = default;
    //! Reserves space for at least `numBits`.
    void reserve(uint32_t numBits);
    //! Returns whether the set contains the given bit.
    [[nodiscard]] bool contains(IndexType bit) const {
        auto [w, p] = idx(bit);
        return w < words() && test_bit(buffer_[w], p);
    }
    //! Returns whether the set is empty.
    [[nodiscard]] bool empty() const noexcept { return buffer_.empty(); }
    //! Returns the number of elements in the set, i.e., the number of bits set.
    [[nodiscard]] auto count() const noexcept -> unsigned;
    //! Returns the smallest element in the set or 0 if empty.
    [[nodiscard]] auto smallest() const noexcept -> unsigned;
    //! Returns the largest element in the set or 0 if empty.
    [[nodiscard]] auto largest() const noexcept -> unsigned;
    //! Returns the number of active words.
    [[nodiscard]] auto words() const noexcept -> uint32_t { return buffer_.size(); }
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
    void                                popZero();

    DynamicArray<SetType> buffer_;
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
                auto t = SystemAllocator::allocate(cap * sizeof(T));
                try {
                    std::uninitialized_value_construct_n(static_cast<T*>(t), cap);
                    arr_ = {static_cast<T*>(t), cap};
                }
                catch (...) {
                    SystemAllocator::deallocate(t, cap * sizeof(T));
                    throw;
                }
            }
            else {
                POTASSCO_FAIL(Errc::length_error, "DynamicHashArray");
            }
        }
    }
    DynamicHashArray(DynamicHashArray&& other) noexcept : arr_(std::exchange(other.arr_, {})) {}
    DynamicHashArray& operator=(DynamicHashArray&& other) noexcept {
        if (data() != other.data()) {
            deallocate();
            arr_ = std::exchange(other.arr_, {});
        }
        return *this;
    }
    ~DynamicHashArray() noexcept { deallocate(); }

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
        deallocate();
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
        POTASSCO_DEBUG_ASSERT(pos != nullptr);
        auto hole = static_cast<uint32_t>(pos - data());
        POTASSCO_DEBUG_ASSERT(hole < capacity());
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
    void deallocate() {
        if constexpr (not std::is_trivially_destructible_v<T>) {
            std::destroy_n(arr_.data(), arr_.size());
        }
        SystemAllocator::deallocate(arr_.data(), arr_.size() * sizeof(T));
    }
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
    POTASSCO_TRIVIALLY_RELOCATABLE();
    using HashType = Bucket::hash_type;

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
    template <std::predicate<Id_t> CmpFunc>
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
    template <std::predicate<Id_t> CmpFunc>
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
template <typename KeyT, typename ValT, HashTableKeyTraits<KeyT> HashTraits>
class DynamicHashTable {
public:
    // ReSharper disable CppInconsistentNaming, CppRedundantTypenameKeyword
    using key_type    = KeyT;
    using map_type    = ValT;
    using traits_type = HashTraits;
    POTASSCO_TRIVIALLY_RELOCATABLE(is_trivially_relocatable_v<KeyT>, is_trivially_relocatable_v<ValT>);
    struct BucketT {
        friend bool                                      operator==(const BucketT&, const BucketT&) = default;
        key_type                                         key{HashTraits::empty()};
        POTASSCO_ATTR_NO_UNIQUE_ADDRESS mutable map_type value{};
    };
    struct TraitsT {
        using size_type = uint32_t;
        using hash_type = uint32_t;
        static constexpr bool used(const BucketT& x) { return x.key != HashTraits::empty(); }
        static constexpr auto hashKey(const BucketT& x) -> hash_type {
            return static_cast<uint32_t>(HashTraits::hashKey(x.key));
        }
    };
    using ArrayType     = DynamicHashArray<BucketT, TraitsT>;
    using pointer       = typename ArrayType::pointer;
    using const_pointer = const BucketT*;
    using size_type     = typename ArrayType::size_type;
    // ReSharper restore CppInconsistentNaming, CppRedundantTypenameKeyword
    //! Creates an empty table.
    DynamicHashTable() = default;
    //! Creates a table with at least `bucketCount` buckets.
    explicit DynamicHashTable(size_type bucketCount) : table_(bucketCount) {
        avail_ = static_cast<uint32_t>(table_.capacity() * 0.75);
    }
    //! Creates a copy of `other`.
    DynamicHashTable(const DynamicHashTable& other) : DynamicHashTable(other.size()) {
        auto todo = other.size();
        POTASSCO_DEBUG_ASSERT(avail_ >= todo);
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
        POTASSCO_DEBUG_ASSERT(nk != HashTraits::empty());
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

//! A simple (linear-probing) hash map that maps integral keys to trivially relocatable values.
template <std::integral KeyT, TriviallyRelocatable ValueT, KeyT Empty>
using SimpleHashMap = DynamicHashTable<KeyT, ValueT, Detail::NumHashTraits<KeyT, Empty>>;

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
    POTASSCO_TRIVIALLY_RELOCATABLE();
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
    static void                  release(const Large&);
    constexpr void               release() {
        if (tag() == c_large_tag) {
            ConstString::release(*large());
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
    POTASSCO_TRIVIALLY_RELOCATABLE();
    OrderedStringSet() = default;
    explicit OrderedStringSet(bool allowShort);
    OrderedStringSet(const OrderedStringSet&)            = delete;
    OrderedStringSet(OrderedStringSet&&) noexcept        = default;
    OrderedStringSet& operator=(const OrderedStringSet&) = delete;
    OrderedStringSet& operator=(OrderedStringSet&&)      = default;
    ~OrderedStringSet()                                  = default;

    //! Returns the number of elements in the set.
    [[nodiscard]] auto size() const -> uint32_t { return strings_.size(); }
    //! Returns a view over the current elements.
    /*!
     * \note The view is only valid until the next call to a mutating function.
     */
    [[nodiscard]] auto elements() const noexcept -> std::span<const ConstString> { return {strings_.data(), size()}; }
    //! Returns a (const) reference to the ith element in the set.
    /*!
     * \note The reference is only valid until the next call to a mutating function.
     */
    [[nodiscard]] auto operator[](Id_t i) const -> const ConstString& {
        POTASSCO_DEBUG_ASSERT(i < size());
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

    DynamicIndex              index_;
    DynamicArray<ConstString> strings_;
    bool                      allowShort_{true};
};

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
template <std::ranges::contiguous_range R, typename RankFn, typename Tb = DynamicArray<std::ranges::range_value_t<R>>>
requires std::is_invocable_v<RankFn&, std::ranges::range_value_t<R>> &&
         std::is_unsigned_v<std::remove_cvref_t<std::invoke_result_t<RankFn&, std::ranges::range_value_t<R>>>>
constexpr void radixSort(R&& rng, RankFn rank, RadixConfig config = radix_def, Tb tmp = {}) {
    using T        = std::ranges::range_value_t<R>;
    using Rank     = std::remove_cvref_t<std::invoke_result_t<RankFn&, T>>;
    using SizeType = std::common_type_t<std::size_t, Rank>;
    static_assert(std::is_nothrow_move_constructible_v<T>);
    POTASSCO_DEBUG_ASSERT(std::size(rng) <= UINT32_MAX);
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
            POTASSCO_DEBUG_ASSERT(p < n);
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
