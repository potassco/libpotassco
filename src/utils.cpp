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
#include <potassco/utils.h>

#include <potassco/error.h>

#include <memory>
#include <numeric>

namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// DynamicBuffer
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr uint32_t c_fast_grow_cap = 0x20000u;
static constexpr uint32_t nextCapacity(uint32_t current) {
    if (current == 0u) {
        return 64u;
    }
    return current <= c_fast_grow_cap ? (current * 3 + 1) >> 1 : current << 1u;
}
DynamicBuffer::DynamicBuffer(std::size_t init) { reserve(init); }
DynamicBuffer::DynamicBuffer(std::span<char> borrow)
    : beg_(borrow.data())
    , cap_(safe_cast<uint32_t>(borrow.size()))
    , sizeOwn_(nth_bit<uint32_t>(borrow_bit)) {
    POTASSCO_ASSERT(sizeOwn_ > size_mask && cap_ < sizeOwn_ && size() == 0);
}
DynamicBuffer::DynamicBuffer(DynamicBuffer&& other) noexcept
    : beg_(std::exchange(other.beg_, nullptr))
    , cap_(std::exchange(other.cap_, 0))
    , sizeOwn_(std::exchange(other.sizeOwn_, 0)) {}
DynamicBuffer::DynamicBuffer(const DynamicBuffer& other) : DynamicBuffer() { append(other.data(), other.size()); }
DynamicBuffer::~DynamicBuffer() { release(); }
DynamicBuffer& DynamicBuffer::operator=(DynamicBuffer&& other) noexcept {
    if (this != &other) {
        DynamicBuffer(std::move(other)).swap(*this);
    }
    return *this;
}
DynamicBuffer& DynamicBuffer::operator=(const DynamicBuffer& other) {
    if (this != &other) {
        DynamicBuffer(other).swap(*this);
    }
    return *this;
}
void DynamicBuffer::release() noexcept {
    if (auto p = std::exchange(beg_, nullptr); p) {
        if (not test_bit(sizeOwn_, borrow_bit)) {
            std::free(p);
        }
        cap_ = sizeOwn_ = 0;
    }
}
void DynamicBuffer::swap(DynamicBuffer& other) noexcept {
    std::swap(beg_, other.beg_);
    std::swap(cap_, other.cap_);
    std::swap(sizeOwn_, other.sizeOwn_);
}
void DynamicBuffer::reserve(std::size_t n) {
    if (n > capacity()) {
        auto  newCap = safe_cast<uint32_t>(std::max(static_cast<std::size_t>(nextCapacity(capacity())), n));
        void* t      = not test_bit(sizeOwn_, borrow_bit) ? std::realloc(beg_, newCap) : std::malloc(newCap);
        POTASSCO_CHECK(t, Errc::bad_alloc);
        if (test_bit(sizeOwn_, borrow_bit)) {
            std::memcpy(t, beg_, size());
            store_clear_bit(sizeOwn_, borrow_bit);
        }
        beg_ = t;
        cap_ = newCap;
    }
}
std::span<char> DynamicBuffer::alloc(std::size_t n) {
    reserve(size() + n);
    return {data(std::exchange(sizeOwn_, static_cast<uint32_t>(sizeOwn_ + n)) & size_mask), n};
}
void DynamicBuffer::append(const void* what, std::size_t n) {
    if (n) {
        std::memcpy(alloc(n).data(), what, n);
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
// DynamicBitset
/////////////////////////////////////////////////////////////////////////////////////////
void DynamicBitset::reserve(unsigned numBits) {
    if (numBits) {
        auto r = (1u + idx(numBits).word) * sizeof(SetType);
        buffer_.reserve(r);
    }
}
auto DynamicBitset::count() const noexcept -> unsigned {
    return std::accumulate(data(), data() + words(), 0u, [](unsigned n, uint64_t w) { return bit_count(w) + n; });
}
auto DynamicBitset::smallest() const noexcept -> unsigned {
    return not empty() ? static_cast<unsigned>(countr_zero(*data())) : 0u;
}
auto DynamicBitset::largest() const noexcept -> unsigned {
    if (auto w = words(); w == 0) {
        return 0u;
    }
    else {
        return ((w - 1) * 64u) + (63u - static_cast<unsigned>(countl_zero(data()[w - 1])));
    }
}
auto DynamicBitset::compare(const DynamicBitset& rhs) const -> std::strong_ordering {
    auto n = words();
    if (auto x = n <=> rhs.words(); x != std::strong_ordering::equal) {
        return x;
    }
    for (const auto *x = data(), *y = rhs.data(); n--;) {
        if (auto cmp = x[n] <=> y[n]; cmp != std::strong_ordering::equal) {
            return cmp;
        }
    }
    return std::strong_ordering::equal;
}
bool DynamicBitset::add(IndexType bit) {
    auto [word, pos] = idx(bit);
    if (word < words()) {
        return test_bit(data()[word], pos) || store_set_bit(data()[word], pos);
    }
    auto missing = (word - words()) + 1u;
    auto mem     = buffer_.alloc(missing * sizeof(SetType));
    std::uninitialized_fill_n(reinterpret_cast<SetType*>(mem.data()), missing, SetType{0});
    store_set_bit(data()[word], pos);
    return true;
}
bool DynamicBitset::remove(IndexType bit) {
    if (auto [word, pos] = idx(bit); word < words() && test_bit(data()[word], pos)) {
        if (store_clear_bit(data()[word], pos) == 0u) {
            compact();
        }
        return true;
    }
    return false;
}
void DynamicBitset::apply(uint64_t mask) {
    auto n = words();
    for (auto* x = data(); n--; ++x) { *x &= mask; }
    compact();
}
void DynamicBitset::compact() {
    const auto w = words();
    auto       n = w;
    for (const auto* d = data(); d[n - 1] == 0u;) { --n; }
    if (n < w) {
        buffer_.pop(sizeof(SetType) * (w - n));
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
// ConstString
/////////////////////////////////////////////////////////////////////////////////////////
ConstString::ConstString(std::string_view n) { // NOLINT(cppcoreguidelines-pro-type-member-init)
    init(n);
}
ConstString::ConstString(const ConstString& o) : ConstString(o.view()) { POTASSCO_DEBUG_ASSERT(tag() != c_borrow_tag); }
ConstString::ConstString(Borrow_t, std::string_view n) { // NOLINT(cppcoreguidelines-pro-type-member-init)
    new (storage_) Large{.str = n.data(), .size = n.size()};
    storage_[c_max_small] = static_cast<char>(c_borrow_tag);
}
void ConstString::init(std::string_view str) {
    char* out = storage_;
    if (str.size() > c_max_small) {
        out = static_cast<char*>(::operator new[](str.size() + 1));
        new (storage_) Large{.str = out, .size = str.size()};
        storage_[c_max_small] = static_cast<char>(c_large_tag);
    }
    else {
        storage_[c_max_small] = static_cast<char>(c_max_small - str.size());
    }
    *std::ranges::copy(str, out).out = 0;
}
void ConstString::release() { delete[] large()->str; }
template <typename C>
static void assignImpl(ConstString* self, C&& source) {
    if (self != &source) {
        self->~ConstString();
        new (self) ConstString(std::forward<C>(source));
    }
}
ConstString& ConstString::operator=(const ConstString& other) { // NOLINT(bugprone-unhandled-self-assignment)
    assignImpl(this, other);
    return *this;
}
ConstString& ConstString::operator=(ConstString&& other) noexcept {
    assignImpl(this, std::move(other));
    return *this;
}

} // namespace Potassco
