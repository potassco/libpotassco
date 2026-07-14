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
void DynamicBuffer::grow(std::size_t n, bool exact) {
    auto nc = std::max<std::size_t>(exact ? n : Detail::nextCap(capacity(), 1u), n);
    if (nc > maxSize()) {
        POTASSCO_CHECK(n <= maxSize(), Errc::length_error);
        nc = maxSize();
    }
    auto* t = not test_bit(sizeOwn_, borrow_bit) ? std::realloc(beg_, nc) : std::malloc(nc);
    POTASSCO_CHECK(t, Errc::bad_alloc);
    if (test_bit(sizeOwn_, borrow_bit)) {
        std::memcpy(t, beg_, size());
        store_clear_bit(sizeOwn_, borrow_bit);
    }
    beg_ = t;
    cap_ = static_cast<uint32_t>(nc);
}
std::span<char> DynamicBuffer::alloc(std::size_t n) {
    if (auto x = size() + n; x > capacity()) {
        grow(x, false);
    }
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
        return not test_bit(data()[word], pos) && store_set_bit(data()[word], pos);
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
    for (const auto* d = data(); n && d[n - 1] == 0u;) { --n; }
    if (n < w) {
        buffer_.pop(sizeof(SetType) * (w - n));
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
// DynamicIndex
/////////////////////////////////////////////////////////////////////////////////////////
DynamicIndex::DynamicIndex(uint32_t bucketCount, double lf) : table_(bucketCount), lf_(static_cast<float>(lf)) {
    POTASSCO_CHECK_PRE(lf >= 0.5 && lf < 1.0);
    grow_ = static_cast<uint32_t>(buckets() * lf);
}
DynamicIndex::DynamicIndex(const DynamicIndex& other) : DynamicIndex(other.size(), other.lf_) {
    POTASSCO_ASSERT(grow_ >= other.size());
    auto todo = other.size();
    for (const auto* x = other.table_.data(); todo; ++x) {
        if (x->used()) {
            *table_.nextUnused(x->hash) = *x;
            --todo;
        }
    }
    grow_ -= other.size();
    size_  = other.size();
}
DynamicIndex::DynamicIndex(DynamicIndex&& other) noexcept
    : table_(std::move(other.table_))
    , size_(other.size_)
    , grow_(other.grow_)
    , tombs_(other.tombs_)
    , lf_(other.lf_) {
    other.discard();
}

DynamicIndex::~DynamicIndex() { discard(); }

DynamicIndex& DynamicIndex::operator=(const DynamicIndex& other) {
    if (this != &other) {
        *this = DynamicIndex(other);
    }
    return *this;
}

DynamicIndex& DynamicIndex::operator=(DynamicIndex&& other) noexcept {
    if (this != &other) {
        table_ = std::move(other.table_);
        size_  = other.size_;
        grow_  = other.grow_;
        tombs_ = other.tombs_;
        lf_    = other.lf_;
        other.discard();
    }
    return *this;
}
void DynamicIndex::grow() {
    table_.grow();
    grow_  = static_cast<uint32_t>(buckets() * static_cast<double>(lf_)) - size_;
    tombs_ = 0u;
}
bool DynamicIndex::erase(IndexRef r) {
    if (r.valid()) {
        assert(static_cast<uint32_t>(r.bucket() - table_.data()) < buckets());
        *const_cast<Bucket*>(r.bucket()) = Bucket{.hash = 0u, .value = id_tomb};
        ++tombs_;
        --size_;
        if (full() && tombs_ >= size_) {
            // Consolidate - turn tombs into free and re-hash used elements.
            // NB: This algo only works because we always start with at least one free slot.
            // NB: Used elements might move to the right, but in that case, such elements keep their position when
            //     they are touched again in a subsequent iteration.
            auto nFree = 0u;
            for (auto& x : std::span{table_.data(), buckets()}) {
                // temporarily remove x (thereby clearing any tomb)...
                auto t  = std::exchange(x, {});
                nFree  += x.value == id_empty;
                if (t.used()) { // ...and re-insert if relevant
                    *table_.nextUnused(t.hash) = t;
                }
            }
            POTASSCO_ASSERT(nFree > 0, "broken probing invariant");
            grow_ += std::exchange(tombs_, 0u);
        }
        return true;
    }
    return false;
}
void DynamicIndex::clear() {
    grow_ += (size_ + tombs_);
    size_ = tombs_ = 0u;
    table_.clear();
}
void DynamicIndex::discard() {
    size_ = tombs_ = grow_ = 0u;
    table_                 = {};
}
/////////////////////////////////////////////////////////////////////////////////////////
// ConstString
/////////////////////////////////////////////////////////////////////////////////////////
static_assert(ConstString(ConstString("small")).size() == 5u);
ConstString::ConstString(Borrow_t, std::string_view n) { // NOLINT(cppcoreguidelines-pro-type-member-init)
    POTASSCO_CHECK(n.size() <= static_cast<std::size_t>(UINT32_MAX), Errc::length_error, "string too large");
    new (storage_) Large{.str = n.data(), .size = static_cast<uint32_t>(n.size()), .pad = {}};
    storage_[c_max_small] = static_cast<char>(c_borrow_tag);
}
ConstString::ConstString(const ConstString& o) : ConstString(o.small() || o.tag() == c_borrow_tag, o.view()) {
    POTASSCO_DEBUG_ASSERT(tag() != c_borrow_tag);
}
void ConstString::release(const char* str) { delete[] str; }
void ConstString::initLarge(std::string_view str) {
    POTASSCO_CHECK(str.size() <= static_cast<std::size_t>(UINT32_MAX), Errc::length_error, "string too large");
    auto* out                                 = static_cast<char*>(::operator new[](str.size() + 1));
    *std::copy_n(str.data(), str.size(), out) = 0;
    new (storage_) Large{.str = out, .size = static_cast<uint32_t>(str.size()), .pad = {}};
    storage_[c_max_small] = static_cast<char>(c_large_tag);
}
auto ConstString::operator=(const ConstString& other) -> ConstString& {
    if (this != &other) {
        if (other.small()) {
            release();
            std::memcpy(storage_, other.storage_, sizeof(storage_));
        }
        else {
            ConstString temp(false, other.view());
            release();
            moveFrom(std::move(temp));
        }
    }
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////////
// OrderedStringSet
/////////////////////////////////////////////////////////////////////////////////////////
OrderedStringSet::OrderedStringSet(bool allowShort) : allowShort_(allowShort) {}
OrderedStringSet::~OrderedStringSet() { clear(); }
void OrderedStringSet::push(std::string_view str) {
    auto* mem = reinterpret_cast<ConstString*>(strings_.alloc(sizeof(ConstString)).data());
    if (allowShort_) {
        std::construct_at(mem, str);
    }
    else {
        std::construct_at(mem, ConstString::NoSso_t{}, str);
    }
}
void OrderedStringSet::clear() {
    index_.clear();
    for (auto& str : elements()) { std::destroy_at(&str); }
    strings_.clear();
}

} // namespace Potassco
