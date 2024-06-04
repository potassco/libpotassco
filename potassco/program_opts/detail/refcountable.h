//
// Copyright (c) 2010-2024 Benjamin Kaufmann
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
//
// NOTE: ProgramOptions is inspired by Boost.Program_options
//       see: www.boost.org/libs/program_options
//
#pragma once

#include <utility>

namespace Potassco::ProgramOptions::detail {

class RefCountable {
public:
    constexpr RefCountable() noexcept : refCount_(1) {}
    constexpr int               addRef() noexcept { return ++refCount_; }
    constexpr int               release() noexcept { return --refCount_; }
    [[nodiscard]] constexpr int refCount() const noexcept { return refCount_; }

private:
    int refCount_;
};

template <typename T>
class IntrusiveSharedPtr {
public:
    using element_type = T;
    constexpr explicit IntrusiveSharedPtr(T* p = nullptr) noexcept : ptr_(p) {}
    constexpr IntrusiveSharedPtr(const IntrusiveSharedPtr& o) noexcept : ptr_(o.ptr_) { addRef(); }
    constexpr IntrusiveSharedPtr(IntrusiveSharedPtr&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    constexpr ~IntrusiveSharedPtr() noexcept { release(); }
    constexpr IntrusiveSharedPtr& operator=(IntrusiveSharedPtr other) noexcept {
        this->swap(other);
        return *this;
    }
    constexpr T&                 operator*() const noexcept { return *ptr_; }
    constexpr T*                 operator->() const noexcept { return ptr_; }
    [[nodiscard]] constexpr T*   get() const noexcept { return ptr_; }
    [[nodiscard]] constexpr bool unique() const noexcept { return ptr_ == nullptr || ptr_->refCount() == 1; }
    [[nodiscard]] constexpr int  count() const noexcept { return ptr_ ? ptr_->refCount() : 0; }

    constexpr void reset() noexcept { release(); }
    constexpr void swap(IntrusiveSharedPtr& b) noexcept { std::swap(ptr_, b.ptr_); }

private:
    constexpr void addRef() const noexcept {
        if (ptr_)
            ptr_->addRef();
    }
    constexpr void release() noexcept {
        if (auto* prev = std::exchange(ptr_, nullptr); prev && prev->release() == 0) {
            delete prev;
        }
    }

    T* ptr_;
};

} // namespace Potassco::ProgramOptions::detail
