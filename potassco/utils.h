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
template <typename It>
constexpr auto distance(It first, It last) -> std::size_t {
    return static_cast<std::size_t>(std::distance(first, last));
}
inline constexpr struct Construct_t {
    template <typename T, typename... Args>
    POTASSCO_ATTR_INLINE constexpr T* operator()(T* out, Args&&... args) const {
        if constexpr (std::is_trivial_v<T>) {
            ((*out = std::forward<Args>(args)), ...);
            return out;
        }
        else {
            return std::construct_at(out, std::forward<Args>(args)...);
        }
    }
} construct{};
inline constexpr struct Copy_t {
    template <typename T, typename InIt>
    POTASSCO_ATTR_INLINE constexpr void operator()(T* out, std::size_t n, InIt first) const {
        using InType = decltype(*first);
        static_assert(std::is_constructible_v<T, InType>, "result type must be constructible from input type");
        if constexpr (std::contiguous_iterator<InIt> && std::is_lvalue_reference_v<InType> &&
                      std::is_constructible_v<const T*, std::add_pointer_t<InType>>) {
            assert(n);
            std::memmove(static_cast<void*>(out), static_cast<const void*>(std::addressof(*first)), n * sizeof(T));
        }
        else {
            std::uninitialized_copy_n(first, n, out);
        }
    }
} copy{};
inline constexpr struct Fill_t {
    template <typename T>
    POTASSCO_ATTR_INLINE constexpr void operator()(T* out, std::size_t n, const T& val) const {
        if constexpr (std::is_nothrow_copy_constructible_v<T>) {
            while (n--) {
                Detail::construct(out, val);
                ++out;
            }
        }
        else {
            std::uninitialized_fill_n(out, n, val);
        }
    }
    template <typename T>
    POTASSCO_ATTR_INLINE constexpr void operator()(T* out, std::size_t n, std::type_identity<T>) const {
        if constexpr (std::is_nothrow_copy_constructible_v<T>) {
            (*this)(out, n, T{});
        }
        else {
            std::uninitialized_value_construct_n(out, n);
        }
    }
} fill{};
template <typename T>
POTASSCO_ATTR_INLINE constexpr void destructiveMove(T* target, T* source) {
    std::memcpy(static_cast<void*>(target), static_cast<void*>(source), sizeof(T));
}
template <typename T>
POTASSCO_ATTR_INLINE constexpr void relocate(T* target, T* source, std::size_t n) {
    if (n) {
        std::memmove(static_cast<void*>(target), static_cast<void*>(source), n * sizeof(T));
    }
}
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
template <typename T>
struct SystemAllocAdaptor {
    using value_type            = T;
    static constexpr auto align = static_cast<std::align_val_t>(alignof(T));
    T*   allocate(std::size_t sz) const { return static_cast<T*>(SystemAllocator::allocate(sz * sizeof(T), align)); }
    void deallocate(T* mem, std::size_t sz) { return SystemAllocator::deallocate(mem, sz * sizeof(T), align); }
    T*   reallocate(T* mem, std::size_t sz) requires(alignof(T) <= SystemAllocator::realloc_max_align)
    {
        return static_cast<T*>(SystemAllocator::reallocate(static_cast<void*>(mem), sz * sizeof(T)));
    }
    auto expand(T* mem, std::size_t sz) -> std::size_t {
        return SystemAllocator::expand(mem, sz * sizeof(T), align) / sizeof(T);
    }
    [[nodiscard]] auto goodAllocSize(std::size_t sz) const noexcept -> std::size_t {
        return SystemAllocator::goodAllocSize(sz * sizeof(T), align) / sizeof(T);
    }
};
template <typename Alloc, typename T>
consteval auto hasConstructOrDestroy() {
    if constexpr (requires { std::declval<Alloc>().construct(static_cast<T*>(nullptr)); }) {
        return std::true_type{};
    }
    else if constexpr (requires { std::declval<Alloc>().destroy(static_cast<T*>(nullptr)); }) {
        return std::true_type{};
    }
    else {
        return std::false_type{};
    }
}
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

template <typename T, typename Alloc = Detail::SystemAllocAdaptor<T>, std::unsigned_integral SizeT = uint32_t>
requires(is_trivially_relocatable_v<T>)
class Vector {
public:
    // NOLINTBEGIN
    //! \name Member types
    //!@{
    using allocator_type                = Alloc;
    using value_type                    = T;
    using size_type                     = SizeT;
    using reference                     = value_type&;
    using const_reference               = const value_type&;
    using difference_type               = std::make_signed_t<size_type>;
    using pointer                       = T*;
    using const_pointer                 = const T*;
    using iterator                      = T*;
    using const_iterator                = const T*;
    using reverse_iterator              = std::reverse_iterator<iterator>;
    using const_reverse_iterator        = std::reverse_iterator<const_iterator>;
    using trivially_relocatable         = std::true_type;
    using value_param_type              = Detail::Param_t<value_type>;
    static constexpr auto value_param_v = std::is_same_v<value_type, value_param_type>;
    static_assert(sizeof(size_type) <= sizeof(std::size_t),
                  "Selected size_type not supported - must be <= std::size_t");
    //!@}
    // NOLINTEND
    //! \name Constructors
    //!@{
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    //! Creates an empty vector.
    /*!
     * \post size() == capacity() == 0
     */
    constexpr Vector() noexcept = default;
    constexpr explicit Vector(const Alloc& alloc) noexcept : alloc_(alloc) {}
    //! Creates a vector with `n` default constructed elements.
    /*!
     * \post size() == n
     */
    explicit constexpr Vector(size_type n, const Alloc& alloc = {}) : Vector(alloc, n, std::type_identity<T>{}) {}
    //! Creates a vector with `n` copies of `value`.
    /*!
     * \post size() == n
     */
    explicit constexpr Vector(size_type n, value_param_type value, const Alloc& alloc = {}) : Vector(alloc, n, value) {}
    //! Creates a vector with the elements provided in `x`.
    /*!
     * \post size() == x.size()
     */
    Vector(std::initializer_list<T> x, const Alloc& alloc = {}) : Vector(x.begin(), x.end(), alloc) {}
    //! Creates a vector with the elements in the given range.
    /*!
     * \post size() == std::distance(first, last)
     */
    template <typename InputIt>
    requires(not std::integral<InputIt>)
    Vector(InputIt first, InputIt last, const Alloc& alloc = {})
        : Vector(alloc, Detail::distance(first, last), first) {}
    //! Copy-constructs a vector from `other`.
    /*!
     * \post size() == other.size()
     */
    constexpr Vector(const Vector& other)
        : Vector(other.begin(), other.end(), AllocTraits::select_on_container_copy_construction(other.alloc_)) {}
    //! Move-constructs a vector by stealing all elements from `other`.
    /*!
     * \post other.empty() and other.capacity() == 0
     */
    constexpr Vector(Vector&& other) noexcept
        : data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0u))
        , cap_(std::exchange(other.cap_, 0u))
        , alloc_(std::move(other.alloc_)) {}
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)
    //! Destroys the vector and all contained elements.
    constexpr ~Vector() {
        destroy(data_, size_);
        deallocate();
    }
    //!@}

    //! \name Assignment
    //!@{
    //! Replaces the contents of this vector with copies of those in `other`.
    constexpr auto operator=(const Vector& other) -> Vector& {
        if (this != &other) {
            if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
                clear();
                alloc_ = other.alloc_;
            }
            assign(other.begin(), other.end());
        }
        return *this;
    }
    //! Replaces this with `other`.
    constexpr auto operator=(Vector&& other) noexcept -> Vector& {
        if (this != &other) {
            static_assert(AllocTraits::propagate_on_container_move_assignment::value ||
                              AllocTraits::is_always_equal::value,
                          "operation not supported - use assign");
            reset();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0u);
            cap_  = std::exchange(other.cap_, 0u);
            if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
                alloc_ = std::move(other.alloc_);
            }
        }
        return *this;
    }
    //! Replaces the contents of this vector with (copies of) the elements in `list`.
    constexpr auto operator=(std::initializer_list<T> list) -> Vector& {
        assign(list.begin(), list.end());
        return *this;
    }
    //! Replaces the contents of this vector with the elements in the given range.
    /*!
     * \pre Neither first nor last are iterators/pointers into *this.
     */
    template <typename InputIt>
    requires(not std::integral<InputIt>)
    constexpr void assign(InputIt first, InputIt last) {
        clear();
        append(first, last);
    }
    //! Replaces the contents of this vector with `n` copies of `value`.
    constexpr void assign(size_type n, value_param_type value) {
        clear();
        append(n, value);
    }
    //! Replaces the contents of this vector with (copies of) the elements in `list`.
    constexpr void assign(std::initializer_list<T> x) { assign(x.begin(), x.end()); }
    //! Returns the allocator associated with the vector.
    [[nodiscard]] constexpr auto get_allocator() const noexcept -> Alloc { return alloc_; }
    //!@}

    //! \name Element Access
    //!@{
    //! Returns a pointer to the first element of the underlying array serving as element storage.
    [[nodiscard]] constexpr auto data() noexcept -> pointer { return data_; }
    //! Returns a pointer to the first element of the underlying array serving as element storage.
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return const_cast<Self*>(this)->data(); }
    //! Returns a reference to the element at the given location `pos`.
    /*!
     * \throw std::out_of_range if pos >= size()
     */
    [[nodiscard]] constexpr auto at(size_type pos) -> reference {
        if (pos < size()) {
            return data()[pos];
        }
        throw std::out_of_range("Potassco::Vector::at");
    }
    //! \copydoc at(size_type)
    [[nodiscard]] constexpr auto at(size_type pos) const -> const_reference { return const_cast<Self*>(this)->at(pos); }
    //! Returns a reference to the element at the given location `pos` without bound checking.
    [[nodiscard]] constexpr auto operator[](size_type pos) -> reference {
        assert(pos < size());
        return data()[pos];
    }
    //! \copydoc operator[](size_type)
    [[nodiscard]] constexpr auto operator[](size_type pos) const -> const_reference {
        return const_cast<Self*>(this)->operator[](pos);
    }
    //! Returns a reference to the first element in the vector.
    /*!
     * \pre not empty().
     */
    [[nodiscard]] constexpr auto front() -> reference { return this->operator[](0u); }
    //! \copydoc front()
    [[nodiscard]] constexpr auto front() const -> const_reference { return this->operator[](0u); }
    //! Returns a reference to the last element in the vector.
    /*!
     * \pre not empty().
     */
    [[nodiscard]] constexpr auto back() -> reference { return this->operator[](size() - 1); }
    //! \copydoc back()
    [[nodiscard]] constexpr auto back() const -> const_reference { return this->operator[](size() - 1); }
    //!@}

    //! \name Iterators
    //!@{
    //! Returns an iterator to the first element of the vector.
    [[nodiscard]] constexpr auto begin() -> iterator { return data(); }
    //! \copydoc begin()
    [[nodiscard]] constexpr auto begin() const -> const_iterator { return data(); }
    //! \copydoc begin()
    [[nodiscard]] constexpr auto cbegin() const noexcept -> const_iterator { return data(); }
    //! Returns an iterator to the element following the last element of the vector.
    [[nodiscard]] constexpr auto end() -> iterator { return begin() + size(); }
    //! \copydoc end()
    [[nodiscard]] constexpr auto end() const -> const_iterator { return begin() + size(); }
    //! \copydoc end()
    [[nodiscard]] constexpr auto cend() const noexcept -> const_iterator { return cbegin() + size(); }
    //! Returns a reverse iterator to the first element of the reversed vector.
    [[nodiscard]] constexpr auto rbegin() noexcept -> reverse_iterator { return revIter(end()); }
    //! \copydoc rbegin()
    [[nodiscard]] constexpr auto rbegin() const noexcept -> const_reverse_iterator { return revIter(end()); }
    //! \copydoc rbegin()
    [[nodiscard]] constexpr auto crbegin() const noexcept -> const_reverse_iterator { return revIter(end()); }
    //! Returns a reverse iterator to the element following the last element of the reversed vector.
    [[nodiscard]] constexpr auto rend() noexcept -> reverse_iterator { return std::make_reverse_iterator(begin()); }
    //! \copydoc rend()
    [[nodiscard]] constexpr auto rend() const noexcept -> const_reverse_iterator { return revIter(begin()); }
    //! \copydoc rend()
    [[nodiscard]] constexpr auto crend() const noexcept -> const_reverse_iterator { return revIter(begin()); }
    //!@}

    //! \name Capacity
    //!@{
    //! Checks if the vector has no elements.
    [[nodiscard]] constexpr bool empty() const { return size() == 0u; }
    //! Returns the number of elements in the vector.
    [[nodiscard]] constexpr auto size() const -> size_type { return size_; }
    //! Returns the number of elements that the vector can hold without requiring reallocation.
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type { return cap_; }
    //! Returns the maximum number of elements the vector can hold.
    [[nodiscard]] constexpr auto max_size() const noexcept -> size_type {
        return static_cast<size_type>(-1) / sizeof(T);
    }
    //! Potentially increases the capacity of the vector.
    /*!
     * \note If `nc` is greater than the current capacity(), new storage is allocated, thereby invalidating all
     *       iterators and references to the elements. Otherwise, the function does nothing.
     */
    constexpr void reserve(size_type nc) {
        if (auto n = nc - size(); nc > capacity() && not tryExpand(n)) {
            reallocate(nc);
        }
    }
    //! Removes unused capacity to the extent possible.
    /*!
     * \note If reallocation occurs, all iterators and references to the elements are invalidated.
     */
    constexpr void shrink_to_fit() {
        if (capacity() && size() < capacity()) {
            auto  reqCap = size();
            auto* mem    = reqCap ? allocate(reqCap) : nullptr;
            Detail::relocate(mem, data(), reqCap);
            deallocate();
            data_ = mem;
            cap_  = reqCap;
        }
    }
    //!@}

    //! \name Modifiers
    //!@{

    //! Erases all elements from the vector.
    constexpr void clear() noexcept { destroy(data_, std::exchange(size_, 0u)); }

    //! Resizes the vector to contain `count` elements.
    /*!
     * \note If `count` is greater than `size()`, additional value-initialized elements are appended.
     * \note If `count` is less than `size()`, excessive elements at the end are erased.
     */
    constexpr void resize(size_type count) {
        if (auto sz = size(); count > sz) {
            append(count - sz);
        }
        else {
            shrink(sz - count);
        }
    }
    //! \copybrief resize(count)
    /*!
     * \note If `count` is greater than `size()`, additional copies of `value` are appended.
     * \note If `count` is less than `size()`, excessive elements at the end are erased.
     */
    constexpr void resize(size_type count, value_param_type value) {
        if (auto sz = size(); count > sz) {
            append(count - sz, value);
        }
        else {
            shrink(sz - count);
        }
    }
    //! Copy-appends the given element to the vector.
    /*!
     * \note If `size()` is equal to `capacity()` before the call, a reallocation takes place and all iterators
     *       and references are invalidated. Otherwise, only the `end()` iterator is invalidated.
     */
    constexpr void push_back(value_param_type value) {
        if (not tryPush(value)) [[unlikely]] {
            if (value_param_v) {
                reallocateNext(1u);
                Detail::construct(end(), value);
            }
            else {
                const_reference v = reallocateNext(1u, value);
                Detail::construct(end(), v);
            }
            ++size_;
        }
    }
    //! Move-appends the given element to the vector.
    //! \copydetails push_back(const_reference)
    constexpr void push_back(T&& value) requires(not value_param_v)
    {
        if (not tryPush(std::move(value))) [[unlikely]] {
            const_reference v = reallocateNext(1u, value);
            Detail::construct(end(), const_cast<T&&>(v));
            ++size_;
        }
    }
    //! Appends a new element to the vector via in-place construction.
    //! \copydetails push_back(const_reference)
    template <typename... Args>
    constexpr auto emplace_back(Args&&... args) -> reference {
        if (not tryPush(std::forward<Args>(args)...)) [[unlikely]] {
            alignas(T) unsigned char tmp[sizeof(T)];
            auto*                    p = Detail::construct(reinterpret_cast<T*>(tmp), std::forward<Args>(args)...);
            reallocateNext(1u);
            Detail::destructiveMove(end(), p);
            ++size_;
        }
        return back();
    }
    //! Removes the last element from the vector.
    /*!
     * \pre not empty().
     */
    constexpr void pop_back() {
        assert(not empty());
        --size_;
        if constexpr (has_dtor) {
            std::destroy_at(end());
        }
    }

    //! Exchanges the contents and capacity of the vector with those of `other`.
    constexpr void swap(Vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(cap_, other.cap_);
        if constexpr (AllocTraits::propagate_on_container_swap::value) {
            using std::swap;
            swap(alloc_, other.alloc_);
        }
    }

    //! Inserts the given element before the given position.
    /*!
     * \note If `size()` is equal to `capacity()` before the call, a reallocation takes place and all iterators
     *       and references are invalidated. Otherwise, iterators and references before `pos` remain valid.
     * \pre pos is a valid iterator.
     * \return an iterator pointing to the inserted value(s).
     */
    constexpr auto insert(const_iterator pos, value_param_type value) -> iterator {
        if (pos == end()) [[unlikely]] {
            push_back(value);
            return end() - 1;
        }
        return insertN(pos, 1u, std::addressof(value),
                       [](pointer insPos, const_pointer val) { Detail::construct(insPos, *val); });
    }
    //! Inserts `count` copies of the given element before the given position.
    constexpr auto insert(const_iterator pos, size_type count, value_param_type value) -> iterator {
        if (pos == end()) [[unlikely]] {
            append(count, value);
            return end() - count;
        }
        return insertN(pos, count, std::addressof(value),
                       [&count](pointer insPos, const_pointer val) { Detail::fill(insPos, count, *val); });
    }
    //! \copydoc insert(const_iterator, value_param_type)
    constexpr auto insert(const_iterator pos, T&& value) -> iterator requires(not value_param_v)
    {
        if (pos == end()) [[unlikely]] {
            push_back(std::move(value));
            return end() - 1;
        }
        return insertN(pos, 1u, std::addressof(value),
                       [](pointer insPos, const_pointer val) { Detail::construct(insPos, const_cast<T&&>(*val)); });
    }
    //! Inserts the elements in the given range (which must not be a subrange of the vector) before the given position.
    /*!
     * \note  If first or last are iterators into *this, the behavior is undefined.
     */
    template <typename It>
    requires(not std::integral<It>)
    constexpr auto insert(const_iterator pos, It first, It last) -> iterator {
        if (pos == end()) [[unlikely]] {
            auto idx = size();
            append(first, last);
            return data_ + idx;
        }
        if (auto n = Detail::distance(first, last); n) {
            return insertN(pos, n, nullptr,
                           [n, first](pointer insPos, const_pointer) { Detail::copy(insPos, n, first); });
        }
        return const_cast<iterator>(pos);
    }
    //! \overload insert(const_iterator, It, It)
    constexpr auto insert(const_iterator pos, std::initializer_list<T> x) -> iterator {
        return insert(pos, x.begin(), x.end());
    }
    template <typename... Args>
    constexpr auto emplace(const_iterator pos, Args&&... args) -> iterator {
        if (pos == end()) [[unlikely]] {
            emplace_back(std::forward<Args>(args)...);
            return end() - 1;
        }
        alignas(T) unsigned char tmp[sizeof(T)];
        auto*                    p = Detail::construct(reinterpret_cast<T*>(tmp), std::forward<Args>(args)...);
        return insertN(pos, 1u, nullptr, [p](pointer insPos, const_pointer) { Detail::destructiveMove(insPos, p); });
    }

    //! Erases the element at the given position.
    /*!
     * \note Invalidates all iterators and references to the elements at or after `pos`.
     * \pre pos != end()
     * \return An iterator to the element following the erased element or end() if `pos` referred to the last element.
     */
    constexpr auto erase(const_iterator pos) -> iterator { return erase(pos, pos + 1); }
    //! Erases the elements in the given range, which must be (a potentially empty) subrange of the vector.
    constexpr auto erase(const_iterator first, const_iterator last) -> iterator {
        if (auto n = static_cast<size_type>(Detail::distance(first, last)); n) {
            destroy(const_cast<pointer>(first), n);
            Detail::relocate(const_cast<pointer>(first), const_cast<pointer>(last),
                             static_cast<size_type>(end() - last));
            size_ -= n;
        }
        return const_cast<iterator>(first);
    }
    //!@}

    //! \name Extensions
    //!@{
    template <typename It>
    requires(not std::integral<It>)
    void append(It first, It last) {
        if (auto n = Detail::distance(first, last); n) {
            expand(n, nullptr);
            Detail::copy(end(), n, first);
            size_ += static_cast<size_type>(n);
        }
    }
    constexpr void append(size_type n, value_param_type value) {
        const_reference v = *expand(n, std::addressof(value));
        Detail::fill(end(), n, v);
        size_ += n;
    }
    constexpr void append(size_type n) {
        expand(n, nullptr);
        Detail::fill(end(), n, std::type_identity<T>{});
        size_ += n;
    }
    constexpr void shrink(size_type n) {
        assert(n <= size());
        destroy(data_ + (size_ - n), n);
        size_ -= n;
    }
    //! Discards all elements of this vector and reduces its capacity to the minimum.
    constexpr void reset() {
        clear();
        deallocate();
        data_ = nullptr;
        cap_  = 0;
    }
    template <std::predicate<value_param_type> Pred>
    auto erase_if(Pred pred) -> size_type {
        auto last    = end();
        auto first   = std::find_if(data_, last, std::ref(pred));
        auto removed = static_cast<size_type>(0);
        if (first != last) {
            auto out = first;
            goto drop;
            for (; first != last; ++first) {
                if (not pred(*first)) {
                    Detail::destructiveMove(out++, first);
                }
                else {
                drop:
                    ++removed;
                    if constexpr (has_dtor) {
                        std::destroy_at(first);
                    }
                }
            }
            size_ -= removed;
        }
        return removed;
    }

    //!@}

private:
    using Self                     = Vector;
    using AllocTraits              = std::allocator_traits<Alloc>;
    static constexpr auto has_dtor = not std::is_trivially_destructible_v<T>;
    static_assert(std::is_same_v<decltype(Detail::hasConstructOrDestroy<Alloc, T>()), std::false_type>,
                  "Complex allocator with construct/destroy not supported");
    template <typename IterType>
    static constexpr auto revIter(IterType it) -> std::reverse_iterator<IterType> {
        return std::make_reverse_iterator(it);
    }
    template <typename Arg>
    Vector(const Alloc& alloc, std::size_t n, const Arg& arg) : alloc_(alloc) {
        if (n) {
            data_ = allocate(n);
            cap_  = static_cast<size_type>(n);
            if constexpr (requires { Detail::fill(data_, n, arg); }) {
                Detail::fill(data_, n, arg);
            }
            else {
                Detail::copy(data_, n, arg);
            }
            size_ = static_cast<size_type>(n);
        }
    }
    POTASSCO_ATTR_INLINE constexpr void destroy(pointer pos, size_type n) {
        if constexpr (has_dtor) {
            std::destroy_n(pos, n);
        }
    }
    constexpr auto allocate(std::size_t n) -> T* {
        assert(std::cmp_less_equal(n, max_size()) && "Vector: max size exceeded");
        return AllocTraits::allocate(alloc_, n);
    }
    constexpr void deallocate() {
        if (auto c = capacity(); c) {
            AllocTraits::deallocate(alloc_, data(), c);
        }
    }
    constexpr void reallocate(size_type newCap) {
        assert(std::cmp_greater(newCap, capacity()));
        if constexpr (requires { alloc_.reallocate(data_, newCap); }) {
            data_ = alloc_.reallocate(data_, newCap);
        }
        else {
            auto* t = AllocTraits::allocate(alloc_, newCap);
            std::memcpy(static_cast<void*>(t), static_cast<void*>(data_), size() * sizeof(T));
            data_ = t;
        }
        cap_ = newCap;
    }
    POTASSCO_ATTR_INLINE constexpr bool tryExpand(std::size_t n) noexcept {
        auto req = size() + n;
        if (std::cmp_less_equal(req, capacity())) {
            return true;
        }
        if constexpr (requires { alloc_.expand(data_, req); }) {
            if (auto got = alloc_.expand(data_, req); got >= req) {
                cap_ = static_cast<size_type>(got);
                return true;
            }
        }
        return false;
    }
    constexpr auto expand(std::size_t n, const_pointer val) -> const_pointer {
        if (not tryExpand(n)) {
            if (not value_param_v && val) {
                val = std::addressof(reallocateNext(n, *val));
            }
            else {
                reallocateNext(n);
            }
        }
        return val;
    }
    constexpr auto nextCap(std::size_t n) -> size_type {
        auto sz = size();
        assert(std::cmp_less_equal(n, max_size() - sz) && "Vector: max size exceeded");
        auto newCap = static_cast<size_type>(n + sz);
        newCap      = std::max(newCap, Detail::nextCap(capacity(), sizeof(T)));
        if constexpr (requires { alloc_.goodAllocSize(newCap); }) {
            newCap = static_cast<size_type>(alloc_.goodAllocSize(newCap));
        }
        return newCap;
    }
    constexpr void reallocateNext(std::size_t n) { reallocate(nextCap(n)); }
    constexpr auto reallocateNext(std::size_t n, const_reference v) -> const_reference {
        if (auto* pv = std::addressof(v); pv >= data() && pv < end()) [[unlikely]] {
            auto pos = static_cast<size_type>(pv - data());
            reallocateNext(n);
            return data_[pos];
        }
        reallocateNext(n);
        return v;
    }
    template <typename... Args>
    constexpr bool tryPush(Args&&... args) {
        if (tryExpand(1u)) [[likely]] {
            Detail::construct(end(), std::forward<Args>(args)...);
            ++size_;
            return true;
        }
        return false;
    }

    template <typename Op>
    constexpr auto insertN(const_pointer pos, std::size_t count, const_pointer value, Op op) -> pointer {
        auto idx  = static_cast<size_type>(pos - data_);
        auto tail = size() - idx;
        if (tryExpand(count)) {
            if (not value_param_v && value >= data_ + idx && value < end()) {
                value += count;
            }
            Detail::relocate(data_ + idx + count, data_ + idx, tail);
            std::move(op)(data_ + idx, value);
        }
        else {
            auto  nc = nextCap(count);
            auto* m  = allocate(nc);
            try {
                std::move(op)(m + idx, value);
            }
            catch (...) {
                AllocTraits::deallocate(alloc_, m, nc);
                throw;
            }
            Detail::relocate(m, data_, idx);
            Detail::relocate(m + idx + count, data_ + idx, tail);
            deallocate();
            data_ = m;
            cap_  = nc;
        }
        size_ += static_cast<size_type>(count);
        return data_ + idx;
    }

    T*                                    data_{nullptr};
    size_type                             size_{0u};
    size_type                             cap_{0u};
    POTASSCO_ATTR_NO_UNIQUE_ADDRESS Alloc alloc_{};
};
template <typename InputIt>
requires(not std::integral<InputIt>)
Vector(InputIt, InputIt) -> Vector<typename std::iterator_traits<InputIt>::value_type>;
template <typename InputIt, typename Alloc>
requires(not std::integral<InputIt>)
Vector(InputIt, InputIt, const Alloc& alloc) -> Vector<typename std::iterator_traits<InputIt>::value_type, Alloc>;

//! \name Non-Member Functions
//!@{
template <typename T, typename Alloc, std::unsigned_integral SizeT>
constexpr bool operator==(const Vector<T, Alloc, SizeT>& lhs, const Vector<T, Alloc, SizeT>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}
template <typename T, typename Alloc, std::unsigned_integral SizeT>
constexpr auto operator<=>(const Vector<T, Alloc, SizeT>& lhs,
                           const Vector<T, Alloc, SizeT>& rhs) -> decltype(std::declval<T>() <=> std::declval<T>()) {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T, typename Alloc, std::unsigned_integral SizeT, typename V>
constexpr auto erase(Vector<T, Alloc, SizeT>& vec, const V& value) -> typename Vector<T, Alloc, SizeT>::size_type {
    return vec.erase_if([&](typename Vector<T, Alloc, SizeT>::value_param_type x) { return x == value; });
}
template <typename T, typename Alloc, std::unsigned_integral SizeT, typename P>
constexpr auto erase_if(Vector<T, Alloc, SizeT>& vec, P pred) -> typename Vector<T, Alloc, SizeT>::size_type {
    return vec.erase_if(std::ref(pred));
}

template <typename T, typename Alloc, std::unsigned_integral SizeT>
constexpr void swap(Vector<T, Alloc, SizeT>& x, Vector<T, Alloc, SizeT>& y) noexcept {
    x.swap(y);
}

template <typename T, typename Alloc, std::unsigned_integral SizeT>
constexpr void reset(Vector<T, Alloc, SizeT>& v) noexcept {
    v.reset();
}
//!@}

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
