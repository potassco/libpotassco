//
// Copyright (c) 2026 - present, Benjamin Kaufmann
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

#include <potassco/platform.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <memory>
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
    constexpr void operator()(T* out, [[maybe_unused]] std::size_t n, Args&&... args) const {
        assert(n == 1);
        if constexpr (std::is_trivial_v<T> && sizeof...(Args) == 1) {
            ((*out = args), ...);
        }
        else {
            std::construct_at(out, std::forward<Args>(args)...);
        }
    }
} construct{};
inline constexpr struct Fill_t {
    template <typename T>
    constexpr void operator()(T* out, std::size_t n, const T& val) const {
        if constexpr (std::is_nothrow_copy_constructible_v<T>) {
            while (n--) {
                if constexpr (std::is_trivial_v<T>) {
                    *out = val;
                }
                else {
                    std::construct_at(out, val);
                }
                ++out;
            }
        }
        else {
            std::uninitialized_fill_n(out, n, val);
        }
    }
} fill{};
inline constexpr struct Copy_t {
    template <typename T, typename InIt>
    constexpr void operator()(T* out, std::size_t n, InIt first) const {
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
inline constexpr struct Init_t {
    template <typename T>
    constexpr void operator()(T* out, std::size_t n) const {
        std::uninitialized_value_construct_n(out, n);
    }
} init{};

template <typename T>
POTASSCO_ATTR_INLINE constexpr void destructiveMove(T* target, T* source) {
    std::memcpy(static_cast<void*>(target), static_cast<void*>(source), sizeof(T));
}
template <typename Alloc>
struct AllocTraits;

template <typename T>
struct AllocTraits<std::allocator<T>> {
    using Alloc                           = std::allocator<T>;
    static constexpr auto propagate_copy  = std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value;
    static constexpr auto propagate_move  = std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value;
    static constexpr auto propagate_swap  = std::allocator_traits<Alloc>::propagate_on_container_swap::value;
    static constexpr auto always_equal    = std::allocator_traits<Alloc>::is_always_equal::value;
    static constexpr auto is_system_alloc = false;
    static constexpr auto selectForCopy(const Alloc& alloc) -> decltype(auto) {
        return std::allocator_traits<Alloc>::select_on_container_copy_construction(alloc);
    }
    static constexpr T* allocateAtLeast(Alloc& alloc, std::type_identity<T>, std::size_t& sz) {
        if constexpr (requires { alloc.allocate_at_least(sz); }) {
            auto [ptr, cnt] = alloc.allocate_at_least(sz);
            sz              = static_cast<std::size_t>(cnt);
            return ptr;
        }
        else {
            return alloc.allocate(sz);
        }
    }
    static constexpr void deallocate(Alloc& alloc, T* mem, std::size_t sz) { alloc.deallocate(mem, sz); }
};
template <>
struct AllocTraits<Potassco::SystemAllocator> {
    using Alloc                           = Potassco::SystemAllocator;
    static constexpr auto propagate_copy  = false;
    static constexpr auto propagate_move  = false;
    static constexpr auto propagate_swap  = false;
    static constexpr auto always_equal    = true;
    static constexpr auto is_system_alloc = true;
    static constexpr auto selectForCopy(const Alloc& alloc) -> Alloc { return alloc; }
    template <typename T>
    static constexpr auto allocateAtLeast(Alloc&, std::type_identity<T>, std::size_t& sz) -> T* {
        constexpr auto align = static_cast<std::align_val_t>(alignof(T));
        auto           bytes = sz * sizeof(T);
        auto*          mem   = static_cast<T*>(Alloc::allocate(bytes, align)); // NOLINT
        sz                   = bytes / sizeof(T);
        return mem;
    }
    template <typename T>
    static constexpr auto expand(Alloc&, T* buf, std::size_t& newSize) -> bool {
        if (SystemAllocator::has_expand) {
            constexpr auto align   = static_cast<std::align_val_t>(alignof(T));
            auto           request = newSize * sizeof(T);
            if (auto got = Alloc::expand(buf, request, align); got >= request) {
                newSize = got / sizeof(T);
                return true;
            }
        }
        return false;
    }
    template <typename T>
    requires(not std::is_same_v<void, T>)
    static constexpr void deallocate(Alloc& alloc, T* mem, std::size_t sz) {
        alloc.deallocate(mem, sz * sizeof(T), static_cast<std::align_val_t>(alignof(T))); // NOLINT
    }
};
} // namespace Detail

template <typename T, typename Alloc = SystemAllocator, std::unsigned_integral SizeT = uint32_t, SizeT N = 0>
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
     * \post size() == capacity() == N
     */
    constexpr Vector() noexcept = default;
    constexpr explicit Vector(const Alloc& alloc) noexcept : alloc_(alloc) {}
    //! Creates a vector with `n` default constructed elements.
    /*!
     * \post size() == n
     */
    explicit constexpr Vector(size_type n, const Alloc& alloc = {}) : Vector(n, alloc, Detail::init) {}
    //! Creates a vector with `n` copies of `value`.
    /*!
     * \post size() == n
     */
    explicit constexpr Vector(size_type n, value_param_type value, const Alloc& alloc = {})
        : Vector(n, alloc, Detail::fill, value) {}
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
        : Vector(Detail::distance(first, last), alloc, Detail::copy, first) {}
    //! Copy-constructs a vector from `other`.
    /*!
     * \post size() == other.size()
     */
    constexpr Vector(const Vector& other)
        : Vector(other.begin(), other.end(), AllocTraits::selectForCopy(other.alloc_)) {}
    //! Move-constructs a vector by stealing all elements from `other`.
    /*!
     * \post other.empty() and other.capacity() == N
     */
    constexpr Vector(Vector&& other) noexcept : alloc_(std::move(other.alloc_)) { rep_.move(std::move(other.rep_)); }
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)
    //! Destroys the vector and all contained elements.
    constexpr ~Vector() {
        if constexpr (requires_dtor) {
            std::destroy_n(data(), size());
        }
        if (std::is_constant_evaluated()) {
            return;
        }
        deallocate(rep_.asBuffer());
    }
    //!@}

    //! \name Assignment
    //!@{
    //! Replaces the contents of this vector with copies of those in `other`.
    constexpr Vector& operator=(const Vector& other) {
        if (this != &other) {
            if constexpr (AllocTraits::propagate_copy) {
                clear();
                alloc_ = other.alloc_;
            }
            assign(other.begin(), other.end());
        }
        return *this;
    }
    //! Replaces this with `other`.
    constexpr Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            static_assert(AllocTraits::propagate_move || AllocTraits::always_equal,
                          "operation not supported - use assign");
            clear();
            auto old = rep_.asBuffer();
            rep_.move(std::move(other.rep_));
            if constexpr (AllocTraits::propagate_move) {
                alloc_ = std::move(other.alloc_);
            }
            deallocate(old);
        }
        return *this;
    }
    //! Replaces the contents of this vector with (copies of) the elements in `list`.
    constexpr Vector& operator=(std::initializer_list<T> list) {
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
    [[nodiscard]] constexpr auto data() noexcept -> pointer { return rep_.data(); }
    //! Returns a pointer to the first element of the underlying array serving as element storage.
    [[nodiscard]] constexpr auto data() const noexcept -> const_pointer { return const_cast<Vector*>(this)->data(); }
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
    [[nodiscard]] constexpr auto at(size_type pos) const -> const_reference {
        return const_cast<Vector*>(this)->at(pos);
    }
    //! Returns a reference to the element at the given location `pos` without bound checking.
    [[nodiscard]] constexpr auto operator[](size_type pos) -> reference {
        assert(pos < size());
        return data()[pos];
    }
    //! \copydoc operator[](size_type)
    [[nodiscard]] constexpr auto operator[](size_type pos) const -> const_reference {
        return const_cast<Vector*>(this)->operator[](pos);
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
    [[nodiscard]] constexpr auto size() const -> size_type { return rep_.size(); }
    //! Returns the number of elements that the vector can hold without requiring reallocation.
    [[nodiscard]] constexpr auto capacity() const noexcept -> size_type { return rep_.capacity(); }
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
        if (nc > capacity()) {
            reserveImpl(nc);
        }
    }
    //! Removes unused capacity to the extent possible.
    /*!
     * \note If reallocation occurs, all iterators and references to the elements are invalidated.
     */
    constexpr void shrink_to_fit() {
        if (auto info = rep_.asBuffer(); info.size() != info.capacity() && info.capacity() > N) {
            if (auto reqCap = info.size(); reqCap > min_grow_cap) {
                auto x = sizeof(T) * reqCap;
                auto y = sizeof(T) * capacity();
                if ((y - x) <= 2 * alignof(max_align_t)) {
                    return;
                }
            }
            decltype(rep_) t;
            t.move(std::move(rep_));
            auto* start = reserveImpl(info.size());
            auto  sz    = relocate(start, t.asBuffer());
            rep_.setSize(sz);
            deallocate(info);
        }
    }
    //!@}

    //! \name Modifiers
    //!@{

    //! Erases all elements from the vector.
    constexpr void clear() noexcept {
        if constexpr (requires_dtor) {
            std::destroy_n(data(), size());
        }
        rep_.setSize(0u);
    }
    //! Resizes the vector to contain `count` elements.
    /*!
     * \note If `count` is greater than `size()`, additional value-initialized elements are appended.
     * \note If `count` is less than `size()`, excessive elements at the end are erased.
     */
    constexpr void resize(size_type count) { resizeImpl(count, Detail::init); }
    //! \copybrief resize(count)
    /*!
     * \note If `count` is greater than `size()`, additional copies of `value` are appended.
     * \note If `count` is less than `size()`, excessive elements at the end are erased.
     */
    constexpr void resize(size_type count, value_param_type value) { resizeImpl(count, Detail::fill, value); }
    //! Copy-appends the given element to the vector.
    /*!
     * \note If `size()` is equal to `capacity()` before the call, a reallocation takes place and all iterators
     *       and references are invalidated. Otherwise, only the `end()` iterator is invalidated.
     */
    constexpr void push_back(const_reference value) { appendImpl(1, Detail::construct, value); }
    //! Move-appends the given element to the vector.
    //! \copydetails push_back(const_reference)
    constexpr void push_back(T&& value) { appendImpl(1, Detail::construct, std::move(value)); }
    //! Appends a new element to the vector via in-place construction.
    //! \copydetails push_back(const_reference)
    template <typename... Args>
    constexpr auto emplace_back(Args&&... args) -> reference {
        appendImpl(1, Detail::construct, std::forward<Args>(args)...);
        return back();
    }
    //! Removes the last element from the vector.
    /*!
     * \pre not empty().
     */
    constexpr void pop_back() {
        auto b = rep_.asBuffer();
        assert(b.size() > 0);
        if constexpr (requires_dtor) {
            std::destroy_at(b.end() - 1);
        }
        rep_.setSize(b.size() - 1);
    }
    //! Exchanges the contents and capacity of the vector with those of `other`.
    constexpr void swap(Vector& other) noexcept {
        rep_.swap(other.rep_);
        if constexpr (AllocTraits::propagate_swap) {
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
        return insertImpl<not value_param_v>(1u, Detail::construct, pos, value);
    }
    //! \copydoc insert(const_iterator, value_param_type)
    constexpr auto insert(const_iterator pos, T&& value) -> iterator requires(not value_param_v)
    {
        return insertImpl(1u, Detail::construct, pos, std::move(value));
    }
    //! Inserts `count` copies of the given element before the given position.
    constexpr auto insert(const_iterator pos, size_type count, value_param_type value) -> iterator {
        return insertImpl<not value_param_v>(count, Detail::fill, pos, value);
    }

    //! Inserts the elements in the given range (which must not be a subrange of the vector) before the given position.
    /*!
     * \note  If first or last are iterators into *this, the behavior is undefined.
     */
    template <typename It>
    requires(not std::integral<It>)
    constexpr auto insert(const_iterator pos, It first, It last) -> iterator {
        constexpr bool check = std::is_same_v<It, const_iterator> || std::is_same_v<It, iterator>;
        return insertImpl<check>(Detail::distance(first, last), Detail::copy, pos, first);
    }
    //! \overload insert(const_iterator, It, It)
    constexpr auto insert(const_iterator pos, std::initializer_list<T> x) -> iterator {
        return insert(pos, x.begin(), x.end());
    }
    //! Inserts a new element via in-place construction before the given position.
    template <typename... Args>
    constexpr auto emplace(const_iterator pos, Args&&... args) -> iterator {
        return insertImpl(1u, Detail::construct, pos, std::forward<Args>(args)...);
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
        if (first != last) {
            auto info = rep_.asBuffer();
            auto r    = static_cast<size_type>(last - first);
            assert(info.contains(first) && info.contains(first + (r - 1)));
            if constexpr (requires_dtor) {
                std::destroy_n(first, r);
            }
            if (auto n = static_cast<size_type>(info.end() - last); n > 0) {
                relocate(const_cast<pointer>(first), {const_cast<pointer>(last), n});
            }
            rep_.setSize(info.sz - r);
        }
        return const_cast<iterator>(first);
    }
    //!@}

    //! \name Extensions
    //!@{
    template <typename It>
    requires(not std::integral<It>)
    void append(It first, It last) {
        if (auto n = Detail::distance(first, last); n > 0) {
            appendImpl(n, Detail::copy, first);
        }
    }
    constexpr void append(size_type n, value_param_type value) {
        if (n > 0) {
            appendImpl(n, Detail::fill, value);
        }
    }
    //! Discards all elements of this vector and reduces its capacity to the minimum.
    constexpr void reset() { *this = Vector{}; }
    //! Similar to resize(), but only default-initializes new elements.
    // TODO
    // void resize_no_init(size_type count) { resizeImpl(count, Detail::Init_t<false>{}); }
    template <std::predicate<value_param_type> Pred>
    auto erase_if(Pred pred) -> size_type {
        auto buffer  = rep_.asBuffer();
        auto end     = buffer.end();
        auto first   = std::find_if(buffer.beg, end, std::ref(pred));
        auto removed = static_cast<size_type>(0);
        if (first != end) {
            auto out = first;
            goto drop;
            for (; first != end; ++first) {
                if (not pred(*first)) {
                    Detail::destructiveMove(out++, first);
                }
                else {
                drop:
                    ++removed;
                    if constexpr (requires_dtor) {
                        std::destroy_at(first);
                    }
                }
            }
            rep_.setSize(buffer.sz - removed);
        }
        return removed;
    }
    //!@}

private:
    static constexpr auto requires_dtor = not std::is_trivially_destructible_v<T>;
    static constexpr auto min_grow_cap  = static_cast<std::size_t>(32) / sizeof(T);
    using AllocTraits                   = Detail::AllocTraits<Alloc>;
    // NOLINTBEGIN
    struct Buffer {
        constexpr Buffer() : beg{nullptr}, sz{0u}, cap(0u) {}
        constexpr Buffer(T* p, size_type s) : beg(p), sz(s), cap(s) {}
        constexpr Buffer(T* p, size_type s, size_type c) : beg(p), sz(s), cap(c) {}
        [[nodiscard]] constexpr auto size() const noexcept -> size_type { return sz; }
        [[nodiscard]] constexpr auto capacity() const noexcept -> size_type { return cap; }
        [[nodiscard]] constexpr auto data() noexcept -> pointer { return beg; }
        [[nodiscard]] constexpr auto asBuffer() noexcept -> Buffer { return *this; }
        //
        void set(const Buffer& r) { *this = r; }
        void setSize(size_type n) { sz = n; }
        void move(Buffer&& other) { *this = std::exchange(other, {}); }
        void swap(Buffer& other) {
            std::swap(beg, other.beg);
            std::swap(sz, other.sz);
            std::swap(cap, other.cap);
        }
        // buffer only
        [[nodiscard]] constexpr auto end() noexcept -> pointer { return beg + sz; }
        [[nodiscard]] constexpr auto avail() const noexcept -> size_type { return cap - sz; }
        [[nodiscard]] constexpr auto contains(const void* p) const noexcept -> bool {
            constexpr auto le = std::less<const void*>{};
            return le(p, beg + sz) && not le(p, beg);
        }
        //
        T*        beg;
        size_type sz;
        size_type cap;
    };
    struct BufferWithSmall {
        constexpr BufferWithSmall() : beg(1u) {}
        [[nodiscard]] constexpr bool isSmall() const noexcept { return (beg & 1u) != 0u; }
        [[nodiscard]] constexpr auto size() const noexcept -> size_type {
            return isSmall() ? static_cast<size_type>(beg >> 1) : large.size;
        }
        [[nodiscard]] constexpr auto capacity() const noexcept -> size_type { return isSmall() ? N : large.cap; }
        [[nodiscard]] constexpr auto data() noexcept -> pointer {
            return isSmall() ? std::launder(reinterpret_cast<pointer>(buf)) : reinterpret_cast<pointer>(beg);
        }
        [[nodiscard]] constexpr auto asBuffer() noexcept -> Buffer {
            return isSmall() ? Buffer{std::launder(reinterpret_cast<pointer>(buf)), static_cast<size_type>(beg >> 1), N}
                             : Buffer{reinterpret_cast<pointer>(beg), large.size, large.cap};
        }
        //
        void set(const Buffer& rep) {
            beg   = reinterpret_cast<uintptr_t>(rep.beg);
            large = {rep.sz, rep.cap};
        }
        void setSize(size_type n) {
            if (isSmall()) {
                beg = (n << 1u) | 1u;
            }
            else {
                large.size = n;
            }
        }
        void move(BufferWithSmall&& other) {
            beg = std::exchange(other.beg, 1u);
            if (not isSmall()) {
                large = std::exchange(other.large, {});
            }
            else if (auto sz = size(); sz) {
                relocate(reinterpret_cast<pointer>(buf), {reinterpret_cast<pointer>(other.buf), sz, N});
            }
        }
        void swap(BufferWithSmall& other) noexcept {
            if (not isSmall() && not other.isSmall()) {
                std::swap(beg, other.beg);
                std::swap(large.size, other.large.size);
                std::swap(large.cap, other.large.cap);
            }
            else {
                BufferWithSmall t;
                auto*           lhs = isSmall() ? &other : this;
                auto*           rhs = lhs == this ? &other : this;
                t.move(std::move(*lhs));
                lhs->move(std::move(*rhs));
                rhs->move(std::move(t));
            }
        }
        struct Large {
            size_type size;
            size_type cap;
        };
        uintptr_t beg;
        union {
            alignas(value_type) unsigned char buf[(N + (N == 0)) * sizeof(value_type)];
            Large large;
        };
    };
    static_assert(sizeof(BufferWithSmall) >= sizeof(Buffer) && alignof(BufferWithSmall) >= alignof(Buffer));
    std::conditional_t<N != 0, BufferWithSmall, Buffer> rep_{};
    // NOLINTEND
    template <typename IterType>
    static constexpr auto revIter(IterType it) -> std::reverse_iterator<IterType> {
        return std::make_reverse_iterator(it);
    }
    template <typename OpT, typename... Args>
    constexpr Vector(std::size_t n, const Alloc& alloc, OpT op, Args&&... args) : alloc_(alloc) { // NOLINT
        if (n > 0) {
            auto* start = reserveImpl(n);
            auto  num   = static_cast<size_type>(n);
            op(start, num, std::forward<Args>(args)...);
            rep_.setSize(num);
        }
    }
    constexpr void deallocate(const Buffer& b) {
        if (b.cap > N) {
            assert(b.beg != nullptr);
            AllocTraits::deallocate(alloc_, b.beg, b.cap);
        }
    }
    auto alloc(const Buffer& info, std::size_t n, bool exact = false) -> Buffer {
        if (auto maxN = max_size() - info.size(); std::cmp_less(maxN, n)) {
            throw std::length_error("Potassco::Vector: max size exceeded");
        }
        auto newCap = n + info.size();
        if (not exact) {
            auto cap = info.capacity();
            if (cap == N) {
                newCap = std::max(min_grow_cap, newCap);
            }
            if (auto x = static_cast<size_type>((cap * 3 + 1) >> 1); std::cmp_less(newCap, x)) {
                newCap = x;
            }
        }
        assert(std::cmp_greater(newCap, capacity()) && std::cmp_greater(newCap, N));
        auto newDyn = AllocTraits::allocateAtLeast(alloc_, std::type_identity<T>{}, newCap);
        return {newDyn, 0u, static_cast<size_type>(newCap)};
    }
    bool tryExpandCap(Buffer& info, std::size_t newCap) {
        if (info.cap > N && AllocTraits::expand(alloc_, info.beg, newCap)) {
            info.cap = static_cast<size_type>(newCap);
            rep_.set(info);
            return true;
        }
        return false;
    }
    bool prepareForExpand(Buffer& info, std::size_t n) {
        if (info.avail() >= n) {
            return true;
        }
        if constexpr (requires { AllocTraits::expand(alloc_, info.beg, n); }) {
            return tryExpandCap(info, info.sz + n);
        }
        return false;
    }
    static auto relocate(void* to, const Buffer& from) noexcept -> size_type {
        if (auto sz = from.size(); sz) {
            std::memmove(to, from.beg, sz * sizeof(T));
            return sz;
        }
        return 0u;
    }
    template <typename OpT, typename... Args>
    constexpr auto reallocInsert(std::size_t n, const Buffer& old, size_type idx, OpT op, Args&&... args) -> pointer {
        auto  r   = alloc(old, n, false);
        auto* pos = r.beg + idx;
        try {
            // Construct element(s) at their final position.
            // NOTE: args might point into the old buffer - by (move)-constructing final elements first, we ensure
            // that a) references remain valid and b) relocated element(s) have the correct "moved-from" state.
            op(pos, n, std::forward<Args>(args)...);
        }
        catch (...) {
            deallocate(r);
            throw;
        }
        r.sz = static_cast<size_type>(n);
        if (auto suffix = old.sz - idx; suffix == 0u) {
            r.sz += relocate(r.beg, old);
        }
        else {
            // relocate prefix
            r.sz += relocate(r.beg, {old.beg, idx});
            // relocate suffix
            r.sz += relocate(r.beg + idx + static_cast<size_type>(n), {old.beg + idx, suffix});
            assert(r.sz - old.sz == static_cast<size_type>(n));
        }
        deallocate(old);
        rep_.set(r);
        return pos;
    }
    template <typename OpT, typename... Args>
    constexpr void appendImpl(std::size_t n, OpT op, Args&&... args) {
        if (auto c = rep_.asBuffer(); prepareForExpand(c, n)) {
            op(c.end(), n, std::forward<Args>(args)...);
            rep_.setSize(c.size() + static_cast<size_type>(n));
        }
        else {
            reallocInsert(n, c, c.size(), op, std::forward<Args>(args)...);
        }
    }
    template <bool CheckInternal = true, typename OpT, typename... Args>
    constexpr auto insertImpl(std::size_t n, OpT op, const_pointer pos, Args&&... args) -> pointer {
        auto insPos = const_cast<pointer>(pos);
        if (auto num = static_cast<size_type>(n); num > 0) {
            if (auto moveRange = Buffer{insPos, static_cast<size_type>(end() - pos)}; moveRange.sz == 0u) {
                appendImpl(n, op, std::forward<Args>(args)...);
                return end() - 1;
            }
            else if (auto c = rep_.asBuffer(); prepareForExpand(c, n)) {
                if constexpr (CheckInternal) {
                    if constexpr (not std::is_same_v<OpT, Detail::Copy_t>) {
                        if ((moveRange.contains(std::addressof(args)) || ...)) {
                            // Create a temporary object as args (might) point into this vector.
                            value_type tmp{std::forward<Args>(args)...};
                            relocate(insPos + n, moveRange);
                            op(insPos, n, std::move(tmp));
                            rep_.setSize(c.size() + static_cast<size_type>(n));
                            return insPos;
                        }
                    }
                    else if (moveRange.contains(args + (n - 1)...)) {
                        throw std::logic_error("Vector::insert: undefined behavior!");
                    }
                }
                relocate(insPos + n, moveRange);
                op(insPos, n, std::forward<Args>(args)...);
                rep_.setSize(c.size() + static_cast<size_type>(n));
            }
            else {
                insPos = reallocInsert(n, c, static_cast<size_type>(insPos - c.beg), op, std::forward<Args>(args)...);
            }
        }
        return insPos;
    }

    template <typename... Args>
    constexpr void resizeImpl(size_type count, const Args&... args) {
        if (auto sz = size(); sz < count) {
            appendImpl(count - sz, args...);
        }
        else if (sz > count) {
            auto n = sz - count;
            if constexpr (requires_dtor) {
                std::destroy_n(end() - n, n);
            }
            rep_.setSize(sz - n);
        }
    }
    constexpr auto reserveImpl(std::size_t nc) -> T* {
        auto b = rep_.asBuffer();
        if (not prepareForExpand(b, nc - b.sz)) {
            auto newRep = alloc(b, nc - b.size(), true);
            newRep.sz   = relocate(newRep.beg, b);
            deallocate(b);
            rep_.set(b = newRep);
        }
        return b.beg;
    }
    POTASSCO_ATTR_NO_UNIQUE_ADDRESS Alloc alloc_{};
};
template <typename InputIt, typename Alloc = SystemAllocator, std::unsigned_integral SizeT = uint32_t, SizeT N = 0>
requires(not std::integral<InputIt>)
Vector(InputIt, InputIt,
       Alloc = Alloc()) -> Vector<typename std::iterator_traits<InputIt>::value_type, Alloc, SizeT, N>;

//! \name Non-Member Functions
//!@{
template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT L, SizeT R>
constexpr bool operator==(const Vector<T, Alloc, SizeT, L>& lhs, const Vector<T, Alloc, SizeT, R>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}
template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT L, SizeT R>
constexpr auto operator<=>(const Vector<T, Alloc, SizeT, L>& lhs,
                           const Vector<T, Alloc, SizeT, R>& rhs) -> decltype(std::declval<T>() <=> std::declval<T>()) {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT N, typename V>
constexpr auto erase(Vector<T, Alloc, SizeT, N>& vec, const V& value) ->
    typename Vector<T, Alloc, SizeT, N>::size_type {
    return vec.erase_if([&](typename Vector<T, Alloc, SizeT, N>::value_param_type x) { return x == value; });
}
template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT N, typename P>
constexpr auto erase_if(Vector<T, Alloc, SizeT, N>& vec, P pred) -> typename Vector<T, Alloc, SizeT, N>::size_type {
    return vec.erase_if(std::ref(pred));
}

template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT N>
constexpr void swap(Vector<T, Alloc, SizeT, N>& x, Vector<T, Alloc, SizeT, N>& y) noexcept {
    x.swap(y);
}

template <typename T, typename Alloc, std::unsigned_integral SizeT, SizeT N>
constexpr void reset(Vector<T, Alloc, SizeT, N>& v) noexcept {
    v.reset();
}
//!@}
template <typename T, auto N, std::unsigned_integral SizeT = uint32_t, typename Alloc = SystemAllocator>
requires(static_cast<std::make_unsigned_t<decltype(N)>>(N) <= std::numeric_limits<SizeT>::max())
using SmallVector = Vector<T, Alloc, SizeT, static_cast<SizeT>(N)>;

} // namespace Potassco
