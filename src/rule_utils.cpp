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

#include <algorithm>
#include <utility>

namespace Potassco {
Rule Rule::normal(HeadType ht, AtomSpan head, LitSpan body) {
    Rule r;
    r.ht   = ht;
    r.head = head;
    r.bt   = BodyType::normal;
    r.cond = body;
    return r;
}
Rule Rule::sum(HeadType ht, AtomSpan head, const Sum& sum) {
    Rule r;
    r.ht   = ht;
    r.head = head;
    r.bt   = BodyType::sum;
    r.agg  = sum;
    return r;
}
Rule Rule::sum(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan lits) { return sum(ht, head, {lits, bound}); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr auto typePos(uint32_t start) -> uint32_t { return static_cast<uint32_t>(start - sizeof(uint32_t)); }
template <typename T>
static T& asObject(std::byte* mem, uint32_t pos) {
    return *reinterpret_cast<T*>(mem + pos); // NB: C++23: start_lifetime_as
}
template <typename T, typename R>
static auto asSpan(std::byte* mem, const R& range) -> std::span<T> {
    return {reinterpret_cast<T*>(mem + range.start), range.size() / sizeof(T)}; // NB: C++23: start_lifetime_as_array
}
RuleBuilder::RuleBuilder(RuleBuilder&& other) noexcept
    : mem_(std::move(other.mem_))
    , head_(std::exchange(other.head_, {}))
    , body_(std::exchange(other.body_, {})) {
    static_assert(TriviallyRelocatable<Buffer> && TriviallyRelocatable<Range>);
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
auto RuleBuilder::clear() -> RuleBuilder& {
    mem_.clear();
    head_ = body_ = {};
    return *this;
}
bool RuleBuilder::isFact() const {
    return headType() == HeadType::disjunctive && bodyType() == BodyType::normal && body_.size() == 0 &&
           head_.size() == sizeof(Atom_t);
}
auto RuleBuilder::type(const Range& r) const noexcept -> uint32_t {
    return r.start > 2u ? asObject<uint32_t>(mem(), typePos(r.start)) : 0u;
}
void RuleBuilder::start(Range& r, uint32_t type, const Weight_t* bound) {
    if (not r.open()) {
        clear();
    }
    if (bound) {
        append(*bound);
    }
    append(type);
    r.start = r.end = mem_.size();
}
void RuleBuilder::clear(Range& r, uint32_t oPos) {
    if (r.start >= oPos) {
        mem_.pop(mem_.size() - oPos);
    }
    r = {};
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Head management
/////////////////////////////////////////////////////////////////////////////////////////
auto RuleBuilder::start(HeadType ht) -> RuleBuilder& {
    start(head_, to_underlying(ht));
    return *this;
}
auto RuleBuilder::addHead(Atom_t a) -> RuleBuilder& {
    appendRange(head_, body_.end, "Head", a);
    return *this;
}
auto RuleBuilder::clearHead() -> RuleBuilder& {
    clear(head_, body_.end);
    return *this;
}
auto RuleBuilder::head() const -> AtomSpan { return asSpan<Atom_t>(mem(), head_); }
bool RuleBuilder::isMinimize() const { return to_underlying(headType()) == enum_max<HeadType>() + 1u; }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Body management
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr auto boundPos(uint32_t start) -> uint32_t {
    return static_cast<uint32_t>(typePos(start) - sizeof(Weight_t));
}
auto RuleBuilder::startBody() -> RuleBuilder& {
    start(body_, to_underlying(BodyType::normal));
    return *this;
}
auto RuleBuilder::startSum(Weight_t bound) -> RuleBuilder& {
    start(body_, to_underlying(BodyType::sum), &bound);
    return *this;
}
auto RuleBuilder::startMinimize(Weight_t prio) -> RuleBuilder& {
    start(head_, enum_max<HeadType>() + 1u);
    start(body_, to_underlying(BodyType::sum), &prio);
    return *this;
}
auto RuleBuilder::addGoal(Lit_t lit) -> RuleBuilder& {
    bodyType() == BodyType::normal ? appendRange(body_, head_.end, "Body", lit)
                                   : appendRange(body_, head_.end, "Sum", WeightLit{.lit = lit, .weight = 1});
    return *this;
}
auto RuleBuilder::addGoal(WeightLit lit) -> RuleBuilder& {
    if (bodyType() == BodyType::normal) {
        POTASSCO_CHECK_PRE(lit.weight == 1, "non-trivial weight literal not supported in normal body");
        appendRange(body_, head_.end, "Body", lit.lit);
    }
    else if (lit.weight != 0) {
        appendRange(body_, head_.end, "Sum", lit);
    }
    return *this;
}
auto RuleBuilder::clearBody() -> RuleBuilder& {
    clear(body_, head_.end);
    return *this;
}
auto RuleBuilder::setBound(Weight_t bound) -> RuleBuilder& {
    POTASSCO_CHECK_PRE(bodyType() != BodyType::normal, "Invalid call to setBound");
    asObject<Weight_t>(mem(), boundPos(body_.start)) = bound;
    return *this;
}
auto RuleBuilder::weaken(BodyType to, bool resetWeights) -> RuleBuilder& {
    POTASSCO_CHECK_PRE(not isMinimize(), "Invalid call to weaken");
    if (auto t = bodyType(); t != BodyType::normal && t != to) {
        auto sLits  = sumLits();
        auto sBound = bound();
        if (to == BodyType::normal) { // drop bound and weights of literals
            asObject<uint32_t>(mem(), boundPos(body_.start)) = static_cast<uint32_t>(to);
            body_.start = body_.end = typePos(body_.start);
            for (auto m = mem(); auto& [lit, _] : sLits) {
                asObject<Lit_t>(m, body_.end)  = lit;
                body_.end                     += static_cast<uint32_t>(sizeof(Lit_t));
            }
            if (body_.start > head_.start) {
                auto drop = static_cast<uint32_t>((sLits.size() * sizeof(Weight_t)) + sizeof(Weight_t));
                mem_.pop(drop);
            }
        }
        else {
            asObject<uint32_t>(mem(), typePos(body_.start)) = static_cast<uint32_t>(to);
            if (not sLits.empty() && resetWeights && to == BodyType::count) { // set weight of all lits to 1
                auto minW = sLits[0].weight;
                for (auto& [_, weight] : sLits) {
                    minW   = std::min(minW, weight);
                    weight = 1;
                }
                setBound((sBound + (minW - 1)) / minW);
            }
        }
    }
    return *this;
}
auto RuleBuilder::body() const -> LitSpan { return asSpan<Lit_t>(mem(), body_); }
auto RuleBuilder::sumLits() const -> std::span<WeightLit> { return asSpan<WeightLit>(mem(), body_); }
auto RuleBuilder::bound() const -> Weight_t {
    return bodyType() != BodyType::normal ? asObject<Weight_t>(mem(), boundPos(body_.start)) : -1;
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
auto RuleBuilder::end(AbstractProgram* out) -> RuleBuilder& {
    if (head_.open()) {
        head_.start = head_.end = 1;
    }
    if (body_.open()) {
        body_.start = body_.end = 2;
    }
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
