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

#include <algorithm>
#include <utility>

namespace Potassco {
Rule_t Rule_t::normal(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    Rule_t r;
    r.ht   = ht;
    r.head = head;
    r.bt   = Body_t::Normal;
    r.cond = body;
    return r;
}
Rule_t Rule_t::sum(Head_t ht, const AtomSpan& head, const Sum_t& sum) {
    Rule_t r;
    r.ht   = ht;
    r.head = head;
    r.bt   = Body_t::Sum;
    r.agg  = sum;
    return r;
}
Rule_t Rule_t::sum(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) {
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
    , body_(std::exchange(other.body_, {})) {}
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
bool RuleBuilder::frozen() const { return (head_.end_flag & 3u) == 3u && (body_.end_flag & 3u) == 3u; }
void RuleBuilder::start(Range& r, uint32_t type, const Weight_t* bound) {
    if (not frozen()) {
        POTASSCO_CHECK_PRE(r.open(), "%s already started", (&r == &head_ ? "Head" : "Body"));
        if (auto& other = &r == &head_ ? body_ : head_; not other.open()) {
            other.end_flag |= 2u;
        }
    }
    else {
        clear();
    }
    if (bound) {
        new (mem_.alloc(sizeof(Weight_t)).data()) Weight_t(*bound);
    }
    auto pos = mem_.size();
    POTASSCO_ASSERT((pos & 3u) == 0u, "unexpected alignment");
    r = Range{.start_type = pos | type, .end_flag = pos | 1u};
}
template <typename T>
void RuleBuilder::extend(Range& r, const T& elem, const char* what) {
    if (not r.started())
        start(r, 0u);
    POTASSCO_CHECK_PRE(not r.finished(), "%s already frozen", what);
    new (mem_.alloc(sizeof(T)).data()) T(elem);
    r.end_flag += sizeof(T);
}
void RuleBuilder::clear(Range& r) {
    if (const auto& other = &r == &head_ ? body_ : head_; r.start() >= other.end())
        mem_.pop(mem_.size() - other.end());
    r = {};
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Head management
/////////////////////////////////////////////////////////////////////////////////////////
RuleBuilder& RuleBuilder::start(Head_t ht) {
    start(head_, static_cast<uint32_t>(ht));
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
Head_t   RuleBuilder::headType() const { return static_cast<Head_t>(head_.type()); }
AtomSpan RuleBuilder::head() const { return makeSpan<Atom_t>(mem_, head_); }
Atom_t*  RuleBuilder::head_begin() const { return storage_cast<Atom_t*>(mem_, head_.start()); }
Atom_t*  RuleBuilder::head_end() const { return storage_cast<Atom_t*>(mem_, head_.end()); }
bool     RuleBuilder::isMinimize() const { return headType() == static_cast<Head_t>(Directive_t::Minimize); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Body management
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr auto boundPos(uint32_t pos) { return pos - sizeof(Weight_t); }
RuleBuilder&          RuleBuilder::startBody() {
    start(body_, static_cast<uint32_t>(Body_t::Normal));
    return *this;
}
RuleBuilder& RuleBuilder::startSum(Weight_t bound) {
    if (not isMinimize() || frozen()) {
        start(body_, static_cast<uint32_t>(Body_t::Sum), &bound);
    }
    return *this;
}
RuleBuilder& RuleBuilder::startMinimize(Weight_t prio) {
    start(head_, static_cast<uint32_t>(Directive_t::Minimize));
    start(body_, static_cast<uint32_t>(Body_t::Sum), &prio);
    return *this;
}
RuleBuilder& RuleBuilder::addGoal(Lit_t lit) {
    bodyType() == Body_t::Normal ? extend(body_, lit, "Body")
                                 : extend(body_, WeightLit_t{.lit = lit, .weight = 1}, "Sum");
    return *this;
}
RuleBuilder& RuleBuilder::addGoal(WeightLit_t lit) {
    POTASSCO_CHECK_PRE(bodyType() != Body_t::Normal, "weight literal not supported in normal body");
    extend(body_, lit, "Sum");
    return *this;
}
RuleBuilder& RuleBuilder::clearBody() {
    clear(body_);
    return *this;
}
RuleBuilder& RuleBuilder::setBound(Weight_t bound) {
    POTASSCO_CHECK_PRE(bodyType() != Body_t::Normal && not frozen(), "Invalid call to setBound");
    *storage_cast<Weight_t*>(mem_, boundPos(body_.start())) = bound;
    return *this;
}
RuleBuilder& RuleBuilder::weaken(Body_t to, bool resetWeights) {
    POTASSCO_CHECK_PRE(not isMinimize(), "Invalid call to weaken");
    if (auto t = bodyType(); t != Body_t::Normal && t != to) {
        auto s           = sum();
        body_.start_type = (to == Body_t::Normal ? boundPos(body_.start()) : body_.start()) | static_cast<uint32_t>(to);
        if (to == Body_t::Normal) { // drop bound and weights of literals
            std::transform(s.lits.begin(), s.lits.end(), lits_begin(), [](WeightLit_t wl) { return wl.lit; });
            auto drop = (s.lits.size() * sizeof(Weight_t)) + sizeof(Weight_t);
            if (body_.start() > head_.start())
                mem_.pop(drop);
            body_.end_flag -= drop;
        }
        else if (not s.lits.empty() && resetWeights && to == Body_t::Count) { // set weight of all lits to 1
            auto minW = s.lits[0].weight;
            for (auto& wl : s.lits) {
                minW                                = std::min(minW, wl.weight);
                const_cast<WeightLit_t&>(wl).weight = 1;
            }
            setBound((s.bound + (minW - 1)) / minW);
        }
    }
    return *this;
}
Body_t   RuleBuilder::bodyType() const { return static_cast<Body_t>(body_.type()); }
LitSpan  RuleBuilder::body() const { return makeSpan<Lit_t>(mem_, body_); }
Lit_t*   RuleBuilder::lits_begin() const { return storage_cast<Lit_t*>(mem_, body_.start()); }
Lit_t*   RuleBuilder::lits_end() const { return storage_cast<Lit_t*>(mem_, body_.end()); }
Sum_t    RuleBuilder::sum() const { return {makeSpan<WeightLit_t>(mem_, body_), bound()}; }
Weight_t RuleBuilder::bound() const {
    return bodyType() != Body_t::Normal ? *storage_cast<Weight_t*>(mem_, boundPos(body_.start())) : -1;
}
WeightLit_t* RuleBuilder::wlits_begin() const { return storage_cast<WeightLit_t*>(mem_, body_.start()); }
WeightLit_t* RuleBuilder::wlits_end() const { return storage_cast<WeightLit_t*>(mem_, body_.end()); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Product
/////////////////////////////////////////////////////////////////////////////////////////
Rule_t RuleBuilder::rule() const {
    if (bodyType() == Body_t::Normal) {
        return Rule_t::normal(headType(), head(), body());
    }
    else {
        return Rule_t::sum(headType(), head(), sum());
    }
}
RuleBuilder& RuleBuilder::end(AbstractProgram* out) {
    head_.end_flag |= 3u;
    body_.end_flag |= 3u;
    if (out) {
        if (bodyType() == Body_t::Normal) {
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
