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
#include <cstring>

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
struct RuleBuilder::Rule {
    Rule() {
        head.init(0, 0);
        body.init(0, 0);
        top = sizeof(Rule);
        fix = 0;
    }
    struct Span {
        template <typename T>
        void init(uint32_t p, T t) {
            mbeg = mend = p;
            mtype       = static_cast<uint32_t>(t);
        }
        [[nodiscard]] constexpr uint32_t len() const { return mend - mbeg; }
        template <typename T>
        [[nodiscard]] constexpr T type() const {
            return static_cast<T>(mtype);
        }

        uint32_t mbeg  : 30;
        uint32_t mtype : 2;
        uint32_t mend;
    };
    uint32_t top : 31;
    uint32_t fix : 1;
    Span     head{};
    Span     body{};
};
namespace {
template <typename T>
inline std::span<const T> span_cast(const MemoryRegion& m, const RuleBuilder::Rule::Span& in) {
    return {static_cast<const T*>(m[in.mbeg]), in.len() / sizeof(T)};
}
template <typename T>
inline RuleBuilder::Rule* push(MemoryRegion& m, RuleBuilder::Rule* r, const T& what) {
    POTASSCO_ASSERT(r == m.begin());
    uint32_t t = r->top, nt = t + sizeof(T);
    if (nt > m.size()) {
        m.grow(nt);
        r = static_cast<RuleBuilder::Rule*>(m.begin());
    }
    new (m[t]) T(what);
    r->top = nt;
    return r;
}
} // namespace
RuleBuilder::RuleBuilder() : mem_(64) { new (mem_.begin()) Rule(); }
RuleBuilder::Rule* RuleBuilder::rule_() const { return static_cast<Rule*>(mem_.begin()); }
RuleBuilder::RuleBuilder(const RuleBuilder& other) {
    mem_.grow(other.rule_()->top);
    std::memcpy(mem_.begin(), other.mem_.begin(), other.rule_()->top);
}
RuleBuilder& RuleBuilder::operator=(const RuleBuilder& other) {
    RuleBuilder(other).swap(*this);
    return *this;
}
RuleBuilder::~RuleBuilder() { mem_.release(); }
void         RuleBuilder::swap(RuleBuilder& other) { mem_.swap(other.mem_); }
RuleBuilder& RuleBuilder::clear() {
    new (mem_.begin()) Rule();
    return *this;
}
RuleBuilder::Rule* RuleBuilder::unfreeze(bool discard) {
    auto* r = rule_();
    if (r->fix) {
        if (not discard) {
            r->fix = 0;
        }
        else {
            clear();
        }
    }
    return r;
}
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Head management
/////////////////////////////////////////////////////////////////////////////////////////
RuleBuilder& RuleBuilder::start(Head_t ht) {
    auto* r = unfreeze(true);
    auto& h = r->head;
    POTASSCO_CHECK_PRE(not h.mbeg || h.len() == 0u, "Invalid second call to start()");
    h.init(r->top, ht);
    return *this;
}
RuleBuilder& RuleBuilder::addHead(Atom_t a) {
    auto* r = rule_();
    POTASSCO_CHECK_PRE(not r->fix, "Invalid call to addHead() on frozen rule");
    if (not r->head.mend) {
        r->head.init(r->top, 0);
    }
    POTASSCO_CHECK_PRE(r->head.mbeg >= r->body.mend, "Invalid call to addHead() after startBody()");
    r            = push(mem_, r, a);
    r->head.mend = r->top;
    return *this;
}
RuleBuilder& RuleBuilder::clearHead() {
    auto* r = unfreeze(false);
    r->top  = std::max(r->body.mend, static_cast<uint32_t>(sizeof(Rule)));
    r->head.init(0, 0);
    return *this;
}
AtomSpan RuleBuilder::head() const { return span_cast<Atom_t>(mem_, rule_()->head); }
Atom_t*  RuleBuilder::head_begin() const { return static_cast<Atom_t*>(mem_[rule_()->head.mbeg]); }
Atom_t*  RuleBuilder::head_end() const { return static_cast<Atom_t*>(mem_[rule_()->head.mend]); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Body management
/////////////////////////////////////////////////////////////////////////////////////////
void RuleBuilder::startBody(Body_t bt, Weight_t bnd) {
    auto* r = unfreeze(true);
    if (not r->body.mend) {
        if (bt != Body_t::Normal) {
            r = push(mem_, r, bnd);
        }
        r->body.init(r->top, bt);
    }
    else {
        POTASSCO_CHECK_PRE(r->body.len() == 0, "Invalid second call to startBody()");
    }
}
RuleBuilder& RuleBuilder::startBody() {
    startBody(Body_t::Normal, -1);
    return *this;
}
RuleBuilder& RuleBuilder::startSum(Weight_t bound) {
    startBody(Body_t::Sum, bound);
    return *this;
}
RuleBuilder& RuleBuilder::startMinimize(Weight_t prio) {
    auto* r = unfreeze(true);
    POTASSCO_CHECK_PRE(not r->head.mbeg && not r->body.mbeg, "Invalid call to startMinimize()");
    r->head.init(r->top, Directive_t::Minimize);
    r = push(mem_, r, prio);
    r->body.init(r->top, Body_t::Sum);
    return *this;
}
RuleBuilder& RuleBuilder::addGoal(WeightLit_t lit) {
    auto* r = rule_();
    POTASSCO_CHECK_PRE(not r->fix, "Invalid call to addGoal() on frozen rule");
    if (not r->body.mbeg) {
        r->body.init(r->top, 0);
    }
    POTASSCO_CHECK_PRE(r->body.mbeg >= r->head.mend, "Invalid call to addGoal() after start()");
    if (lit.weight == 0) {
        return *this;
    }
    r            = bodyType() == Body_t::Normal ? push(mem_, r, lit.lit) : push(mem_, r, lit);
    r->body.mend = r->top;
    return *this;
}
RuleBuilder& RuleBuilder::setBound(Weight_t bound) {
    POTASSCO_CHECK_PRE(not rule_()->fix && bodyType() != Body_t::Normal, "Invalid call to setBound()");
    std::memcpy(bound_(), &bound, sizeof(Weight_t));
    return *this;
}
RuleBuilder& RuleBuilder::clearBody() {
    auto* r = unfreeze(false);
    r->top  = std::max(r->head.mend, static_cast<uint32_t>(sizeof(Rule)));
    r->body.init(0, 0);
    return *this;
}
RuleBuilder& RuleBuilder::weaken(Body_t to, bool w) {
    auto* r = rule_();
    if (r->body.type<Body_t>() == Body_t::Normal || r->body.type<Body_t>() == to) {
        return *this;
    }
    WeightLit_t *bIt = wlits_begin(), *bEnd = wlits_end();
    if (to == Body_t::Normal) {
        uint32_t i = r->body.mbeg - sizeof(Weight_t);
        r->body.init(i, 0);
        for (; bIt != bEnd; ++bIt, i += sizeof(Lit_t)) { new (mem_[i]) Lit_t(bIt->lit); }
        r->body.mend = i;
        r->top       = std::max(r->head.mend, r->body.mend);
    }
    else if (to == Body_t::Count && w && bIt != bEnd) {
        Weight_t bnd = *bound_(), min = bIt->weight;
        for (; bIt != bEnd; ++bIt) {
            if (min > bIt->weight) {
                min = bIt->weight;
            }
            bIt->weight = 1;
        }
        setBound((bnd + (min - 1)) / min);
    }
    r->body.mtype = static_cast<uint32_t>(to);
    return *this;
}
Body_t       RuleBuilder::bodyType() const { return rule_()->body.type<Body_t>(); }
LitSpan      RuleBuilder::body() const { return span_cast<Lit_t>(mem_, rule_()->body); }
Lit_t*       RuleBuilder::lits_begin() const { return static_cast<Lit_t*>(mem_[rule_()->body.mbeg]); }
Lit_t*       RuleBuilder::lits_end() const { return static_cast<Lit_t*>(mem_[rule_()->body.mend]); }
Sum_t        RuleBuilder::sum() const { return {span_cast<WeightLit_t>(mem_, rule_()->body), bound()}; }
WeightLit_t* RuleBuilder::wlits_begin() const { return static_cast<WeightLit_t*>(mem_[rule_()->body.mbeg]); }
WeightLit_t* RuleBuilder::wlits_end() const { return static_cast<WeightLit_t*>(mem_[rule_()->body.mend]); }
Weight_t     RuleBuilder::bound() const { return bodyType() != Body_t::Normal ? *bound_() : -1; }
Weight_t*    RuleBuilder::bound_() const { return static_cast<Weight_t*>(mem_[rule_()->body.mbeg - sizeof(Weight_t)]); }
/////////////////////////////////////////////////////////////////////////////////////////
// RuleBuilder - Product
/////////////////////////////////////////////////////////////////////////////////////////
RuleBuilder& RuleBuilder::end(AbstractProgram* out) {
    auto* r = rule_();
    r->fix  = 1;
    if (not out) {
        return *this;
    }
    if (auto isMinimize = static_cast<Directive_t>(r->head.mtype) == Directive_t::Minimize; isMinimize) {
        out->minimize(*bound_(), sum().lits);
    }
    else if (r->body.type<Body_t>() == Body_t::Normal) {
        out->rule(r->head.type<Head_t>(), head(), body());
    }
    else {
        out->rule(r->head.type<Head_t>(), head(), *bound_(), sum().lits);
    }
    return *this;
}
Rule_t RuleBuilder::rule() const {
    auto*       r = rule_();
    const auto& h = r->head;
    const auto& b = r->body;
    Rule_t      ret;
    ret.ht   = h.type<Head_t>();
    ret.head = head();
    ret.bt   = b.type<Body_t>();
    if (ret.bt == Body_t::Normal) {
        ret.cond = body();
    }
    else {
        ret.agg = sum();
    }
    return ret;
}

} // namespace Potassco
