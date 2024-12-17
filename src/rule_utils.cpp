//
// Copyright (c) 2016 - present, Benjamin Kaufmann
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
#include <potassco/rule_utils.h>

#include <potassco/error.h>

#include <amc/type_traits.hpp>

#include <algorithm>
#include <utility>

namespace Potassco {
Rule Rule::normal(HeadType ht, const AtomSpan& head, const LitSpan& body) {
    Rule r;
    r.ht   = ht;
    r.head = head;
    r.bt   = BodyType::normal;
    r.cond = body;
    return r;
}
Rule Rule::sum(HeadType ht, const AtomSpan& head, const Sum& sum) {
    Rule r;
    r.ht   = ht;
    r.head = head;
    r.bt   = BodyType::sum;
    r.agg  = sum;
    return r;
}
Rule Rule::sum(HeadType ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) {
    return sum(ht, head, {lits, bound});
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder
/////////////////////////////////////////////////////////////////////////////////////////
template <typename T>
requires(std::is_pointer_v<T>)
static T storage_cast(const DynamicBuffer& mem, uint32_t pos) {
    return reinterpret_cast<T>(mem.data(pos));
}
template <typename T, typename R>
static std::span<T> makeSpan(const DynamicBuffer& mem, const R& range) {
    return {storage_cast<T*>(mem, range.start()), (range.end() - range.start()) / sizeof(T)};
}
RuleBuilder::RuleBuilder(RuleBuilder&& other) noexcept
    : mem_(std::move(other.mem_))
    , head_(std::exchange(other.head_, {}))
    , body_(std::exchange(other.body_, {})) {
    static_assert(amc::is_trivially_relocatable_v<DynamicBuffer> && amc::is_trivially_relocatable_v<Range>);
}
RuleBuilder& RuleBuilder::operator=(RuleBuilder&& other) noexcept {
    if (this != &other) {
        RuleBuilder(std::move(other)).swap(*this);
    }
    return *this;
}
void RuleBuilder::swap(RuleBuilder& other) noexcept {
    mem_.swap(other.mem_);
    std::swap(head_, other.head_);
    std::swap(body_, other.body_);
}
RuleBuilder& RuleBuilder::clear() {
    mem_.clear();
    head_ = body_ = {};
    return *this;
}
bool RuleBuilder::frozen() const {
    return test_mask(head_.end_flag, Range::mask) && test_mask(body_.end_flag, Range::mask);
}
void RuleBuilder::start(Range& r, uint32_t type, const Weight_t* bound) {
    if (not frozen()) {
        POTASSCO_CHECK_PRE(r.open(), "%s already started", (&r == &head_ ? "Head" : "Body"));
        if (auto& other = &r == &head_ ? body_ : head_; not other.open()) {
            store_set_bit(other.end_flag, Range::end_bit);
        }
    }
    else {
        clear();
    }
    if (bound) {
        new (mem_.alloc(sizeof(Weight_t)).data()) Weight_t(*bound);
    }
    auto pos = mem_.size();
    POTASSCO_ASSERT(not test_any(pos, Range::mask), "unexpected alignment");
    r = Range{.start_type = set_mask(pos, type), .end_flag = set_bit(pos, Range::start_bit)};
}
template <typename T>
void RuleBuilder::extend(Range& r, const T& elem, const char* what) {
    if (not r.started()) {
        start(r, 0u);
    }
    POTASSCO_CHECK_PRE(not r.finished(), "%s already frozen", what);
    new (mem_.alloc(sizeof(T)).data()) T(elem);
    r.end_flag += sizeof(T);
}
void RuleBuilder::clear(Range& r) {
    if (const auto& other = &r == &head_ ? body_ : head_; r.start() >= other.end()) {
        mem_.pop(mem_.size() - other.end());
    }
    r = {};
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Head management
/////////////////////////////////////////////////////////////////////////////////////////
RuleBuilder& RuleBuilder::start(HeadType ht) {
    start(head_, to_underlying(ht));
    return *this;
}
RuleBuilder& RuleBuilder::addHead(Atom_t a) {
    extend(head_, a, "Head");
    return *this;
}
RuleBuilder& RuleBuilder::clearHead() {
    clear(head_);
    return *this;
}
HeadType RuleBuilder::headType() const { return static_cast<HeadType>(head_.type()); }
AtomSpan RuleBuilder::head() const { return makeSpan<Atom_t>(mem_, head_); }
bool     RuleBuilder::isMinimize() const { return headType() == static_cast<HeadType>(AspifType::minimize); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Body management
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr auto boundPos(uint32_t pos) { return static_cast<uint32_t>(pos - sizeof(Weight_t)); }
RuleBuilder&          RuleBuilder::startBody() {
    start(body_, to_underlying(BodyType::normal));
    return *this;
}
RuleBuilder& RuleBuilder::startSum(Weight_t bound) {
    if (not isMinimize() || frozen()) {
        start(body_, to_underlying(BodyType::sum), &bound);
    }
    return *this;
}
RuleBuilder& RuleBuilder::startMinimize(Weight_t prio) {
    start(head_, to_underlying(AspifType::minimize));
    start(body_, to_underlying(BodyType::sum), &prio);
    return *this;
}
RuleBuilder& RuleBuilder::addGoal(Lit_t lit) {
    bodyType() == BodyType::normal ? extend(body_, lit, "Body")
                                   : extend(body_, WeightLit{.lit = lit, .weight = 1}, "Sum");
    return *this;
}
RuleBuilder& RuleBuilder::addGoal(WeightLit lit) {
    if (bodyType() == BodyType::normal) {
        POTASSCO_CHECK_PRE(lit.weight == 1, "non-trivial weight literal not supported in normal body");
        extend(body_, lit.lit, "Body");
    }
    else {
        extend(body_, lit, "Sum");
    }
    return *this;
}
RuleBuilder& RuleBuilder::clearBody() {
    clear(body_);
    return *this;
}
RuleBuilder& RuleBuilder::setBound(Weight_t bound) {
    POTASSCO_CHECK_PRE(bodyType() != BodyType::normal && not frozen(), "Invalid call to setBound");
    *storage_cast<Weight_t*>(mem_, boundPos(body_.start())) = bound;
    return *this;
}
RuleBuilder& RuleBuilder::weaken(BodyType to, bool resetWeights) {
    POTASSCO_CHECK_PRE(not isMinimize(), "Invalid call to weaken");
    if (auto t = bodyType(); t != BodyType::normal && t != to) {
        auto sLits       = sumLits();
        auto sBound      = bound();
        body_.start_type = (to == BodyType::normal ? boundPos(body_.start()) : body_.start()) | to_underlying(to);
        if (to == BodyType::normal) { // drop bound and weights of literals
            std::ranges::transform(sLits, storage_cast<Lit_t*>(mem_, body_.start()),
                                   [](WeightLit wl) { return wl.lit; });
            auto drop = (sLits.size() * sizeof(Weight_t)) + sizeof(Weight_t);
            if (body_.start() > head_.start()) {
                mem_.pop(drop);
            }
            body_.end_flag -= static_cast<uint32_t>(drop);
        }
        else if (not sLits.empty() && resetWeights && to == BodyType::count) { // set weight of all lits to 1
            auto minW = sLits[0].weight;
            for (auto& wl : sLits) {
                minW      = std::min(minW, wl.weight);
                wl.weight = 1;
            }
            setBound((sBound + (minW - 1)) / minW);
        }
    }
    return *this;
}
BodyType RuleBuilder::bodyType() const { return static_cast<BodyType>(body_.type()); }
LitSpan  RuleBuilder::body() const { return makeSpan<Lit_t>(mem_, body_); }
auto     RuleBuilder::sumLits() const -> std::span<WeightLit> { return makeSpan<WeightLit>(mem_, body_); }
Weight_t RuleBuilder::bound() const {
    return bodyType() != BodyType::normal ? *storage_cast<Weight_t*>(mem_, boundPos(body_.start())) : -1;
}
Sum  RuleBuilder::sum() const { return {sumLits(), bound()}; }
auto RuleBuilder::findSumLit(Lit_t lit) const -> WeightLit* {
    for (auto& wl : sumLits()) {
        if (wl.lit == lit) {
            return &wl;
        }
    }
    return nullptr;
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Product
/////////////////////////////////////////////////////////////////////////////////////////
Rule RuleBuilder::rule() const {
    Rule ret;
    ret.ht   = headType();
    ret.head = head();
    ret.bt   = bodyType();
    if (ret.bt == BodyType::normal) {
        ret.cond = body();
    }
    else {
        ret.agg = sum();
    }
    return ret;
}
RuleBuilder& RuleBuilder::end(AbstractProgram* out) {
    store_set_mask(head_.end_flag, Range::mask);
    store_set_mask(body_.end_flag, Range::mask);
    if (out) {
        if (bodyType() == BodyType::normal) {
            out->rule(headType(), head(), body());
        }
        else if (auto s = sum(); isMinimize()) {
            out->minimize(s.bound, s.lits);
        }
        else {
            out->rule(headType(), head(), s.bound, s.lits);
        }
    }
    return *this;
}

} // namespace Potassco
