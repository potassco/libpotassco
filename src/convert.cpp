//
// Copyright (c) 2015 - present, Benjamin Kaufmann
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
#include <potassco/convert.h>

#include <potassco/error.h>
#include <potassco/rule_utils.h>
#include <potassco/smodels.h>

POTASSCO_WARNING_BEGIN_RELAXED
#include <amc/vector.hpp>
POTASSCO_WARNING_END_RELAXED

#include <algorithm>
#include <charconv>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace Potassco {
using namespace std::literals;
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert::SmData
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsConvert::SmData {
    using ScratchType = DynamicBuffer;
    template <std::integral T>
    static void append(ScratchType& scratch, T in) {
        char tmp[std::numeric_limits<T>::digits10 + 1];
        auto sz = std::to_chars(tmp, std::end(tmp), in).ptr - tmp;
        scratch.append(tmp, static_cast<std::size_t>(sz));
    }
    static void append(ScratchType& scratch, std::string_view n) { scratch.append(n.data(), n.size()); }
    template <typename... Args>
    static std::string_view makePred(ScratchType& scratch, std::string_view name, Args... args) {
        static_assert(sizeof...(Args) > 0, "at least one arg expected");
        scratch.clear();
        scratch.append(name.data(), name.size());
        scratch.push('(');
        ((append(scratch, args), scratch.push(',')), ...);
        scratch.back() = ')';
        return scratch.view();
    }
    struct Atom {
        std::string_view     makePred(ScratchType& scratch) const { return SmData::makePred(scratch, "_atom"sv, smId); }
        [[nodiscard]] Atom_t sm() const { return smId; }

        uint32_t smId : 28 {0}; // corresponding smodels atom
        uint32_t head : 1 {0};  // atom occurs in a head of a rule
        uint32_t show : 1 {0};  // atom has a name
        uint32_t extn : 2 {0};  // value if atom is external
    };
    struct Heuristic {
        std::string_view makePred(ScratchType& scratch, std::string_view atomName) const {
            return SmData::makePred(scratch, "_heuristic"sv, atomName, enum_name(type), bias, prio);
        }
        Atom_t      atom;
        DomModifier type;
        int         bias{};
        unsigned    prio{};
        unsigned    cond{};
    };
    using SymTab = std::unordered_map<Atom_t, ConstString>;
    struct Output {
        enum Type : uint8_t { type_name = 0, type_edge = 1 };
        struct EdgeT {
            int32_t s;
            int32_t t;
        };
        explicit Output(SymTab::iterator it) : atom(it->first), type(type_name), name(&it->second) {}
        Output(Atom_t a, int32_t s, int32_t t) : atom(a), type(type_edge), edge{s, t} {}
        std::string_view makePred(ScratchType& scratch) const {
            return type == type_name ? name->view() : SmData::makePred(scratch, "_edge"sv, edge.s, edge.t);
        }
        uint32_t atom : 31;
        uint32_t type : 1;
        union {
            const ConstString* name{};
            EdgeT              edge;
        };
    };
    static_assert(amc::is_trivially_relocatable_v<Atom> && amc::is_trivially_relocatable_v<Atom_t> &&
                  amc::is_trivially_relocatable_v<Lit_t> && amc::is_trivially_relocatable_v<WeightLit> &&
                  amc::is_trivially_relocatable_v<Heuristic> && amc::is_trivially_relocatable_v<Output>);
    using AtomMap = amc::vector<Atom>;
    using AtomVec = amc::vector<Atom_t>;
    using WLitVec = amc::vector<WeightLit>;
    using HeuVec  = amc::vector<Heuristic>;
    using OutVec  = amc::vector<Output>;
    struct Minimize {
        static_assert(amc::is_trivially_relocatable_v<WLitVec>);
        using trivially_relocatable = std::true_type; // NOLINT
        Weight_t prio;
        unsigned startPos;
        unsigned endPos;
    };
    static constexpr Atom_t false_atom = 1;
    static_assert(amc::is_trivially_relocatable_v<Minimize>);
    using MinSet = amc::vector<Minimize>;
    SmData()     = default;
    Atom_t newAtom() { return next++; }
    bool   mapped(Atom_t a) const { return a < atoms.size() && atoms[a].smId != 0; }
    Atom&  mapAtom(Atom_t a) {
        if (mapped(a)) {
            return atoms[a];
        }
        if (a >= atoms.size()) {
            atoms.resize(a + 1);
        }
        atoms[a].smId = next++;
        return atoms[a];
    }
    Lit_t mapLit(Lit_t in) {
        auto x = static_cast<Lit_t>(mapAtom(atom(in)).sm());
        return in < 0 ? -x : x;
    }
    WeightLit mapLit(WeightLit in) {
        in.lit = mapLit(in.lit);
        return in;
    }
    Atom_t mapHeadAtom(Atom_t a) {
        Atom& x = mapAtom(a);
        x.head  = 1;
        return x.sm();
    }
    RuleBuilder& mapHead(const AtomSpan& h, HeadType ht = HeadType::disjunctive) {
        rule.clear().start(ht);
        for (auto a : h) { rule.addHead(mapHeadAtom(a)); }
        if (h.empty()) {
            rule.addHead(false_atom);
        }
        return rule;
    }
    template <class T>
    RuleBuilder& mapBody(const std::span<const T>& in) {
        for (const auto& x : in) { rule.addGoal(mapLit(x)); }
        return rule;
    }
    SymTab::iterator addOutput(Atom_t atom, const std::string_view& str) {
        auto [it, added] = symTab.try_emplace(atom, str);
        POTASSCO_CHECK_PRE(added, "Redefinition: atom '%u:%.*s' already shown as '%s'", atom,
                           static_cast<int>(str.size()), str.data(), it->second.c_str());
        output.emplace_back(it);
        return it;
    }

    void addMinimize(Weight_t prio, const WeightLitSpan& lits) {
        if (minimize.empty() || minimize.back().prio != prio) {
            minimize.push_back({.prio = prio, .startPos = minLits.size(), .endPos = minLits.size()});
        }
        auto& vec = minimize.back();
        POTASSCO_ASSERT(vec.endPos == minLits.size());
        for (auto x : lits) {
            if (weight(x) < 0) {
                x.lit    = -x.lit;
                x.weight = -x.weight;
            }
            minLits.push_back(x);
        }
        vec.endPos = minLits.size();
    }
    void addExternal(Atom_t a, TruthValue v) {
        if (auto& ma = mapAtom(a); not ma.head) {
            ma.extn = static_cast<unsigned>(v);
            external.push_back(a);
        }
    }
    void addHeuristic(Atom_t a, DomModifier t, int bias, unsigned prio, Atom_t cond) {
        Heuristic h = {a, t, bias, prio, cond};
        heuristic.push_back(h);
    }
    void flushStep() {
        std::exchange(minimize, {});
        std::exchange(minLits, {});
        std::exchange(external, {});
        std::exchange(heuristic, {});
        output.clear();
    }

    AtomMap     atoms;     // maps input atoms to output atoms
    SymTab      symTab;    // maps output atoms to their names
    AtomVec     external;  // external atoms
    HeuVec      heuristic; // list of heuristic modifications not yet processed
    MinSet      minimize;  // set of minimize constraints
    WLitVec     minLits;   // minimize literals
    OutVec      output;    // list of output atoms not yet processed
    RuleBuilder rule;      // active (mapped) rule
    Atom_t      next{2};   // next unused output atom
};
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert
/////////////////////////////////////////////////////////////////////////////////////////
SmodelsConvert::SmodelsConvert(AbstractProgram& out, bool ext)
    : out_(out)
    , data_(std::make_unique<SmData>())
    , ext_(ext) {}
SmodelsConvert::~SmodelsConvert() = default;
Lit_t    SmodelsConvert::get(Lit_t in) const { return data_->mapLit(in); }
unsigned SmodelsConvert::maxAtom() const { return data_->next - 1; }
Atom_t   SmodelsConvert::makeAtom(const LitSpan& cond, bool named) {
    Atom_t id;
    if (cond.size() != 1 || cond[0] < 0 || (data_->mapAtom(atom(cond[0])).show && named)) {
        // aux :- cond.
        data_->rule.clear().addHead(id = data_->newAtom());
        data_->mapBody(cond).end(&out_);
    }
    else {
        SmData::Atom& ma = data_->mapAtom(atom(cond.front()));
        ma.show          = static_cast<unsigned>(named);
        id               = ma.smId;
    }
    return id;
}
void SmodelsConvert::initProgram(bool inc) { out_.initProgram(inc); }
void SmodelsConvert::beginStep() { out_.beginStep(); }
void SmodelsConvert::rule(HeadType ht, const AtomSpan& head, const LitSpan& body) {
    if (not head.empty() || ht == HeadType::disjunctive) {
        data_->mapHead(head, ht).startBody();
        data_->mapBody(body).end(&out_);
    }
}
void SmodelsConvert::rule(HeadType ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (not head.empty() || ht == HeadType::disjunctive) {
        POTASSCO_CHECK_PRE(std::ranges::none_of(body, [](const auto wl) { return weight(wl) < 0; }),
                           "negative weights in body are not supported");
        if (bound <= 0) {
            SmodelsConvert::rule(ht, head, {});
            return;
        }
        data_->mapHead(head, ht).startSum(bound);
        data_->mapBody(body);
        auto mHead = data_->rule.head();
        auto mBody = data_->rule.sum().lits;
        if (ht == HeadType::disjunctive && mHead.size() == 1) {
            data_->rule.end(&out_);
            return;
        }
        auto auxH = data_->newAtom();
        auto auxB = lit(auxH);
        out_.rule(HeadType::disjunctive, toSpan(auxH), bound, mBody);
        out_.rule(ht, mHead, toSpan(auxB));
    }
}

void SmodelsConvert::minimize(Weight_t prio, const WeightLitSpan& lits) { data_->addMinimize(prio, lits); }
void SmodelsConvert::output(const std::string_view& str, const LitSpan& cond) {
    // create a unique atom for cond and set its name to str
    data_->addOutput(makeAtom(cond, true), str);
}

void SmodelsConvert::external(Atom_t a, TruthValue v) { data_->addExternal(a, v); }
void SmodelsConvert::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, const LitSpan& cond) {
    if (not ext_) {
        out_.heuristic(a, t, bias, prio, cond);
    }
    // create unique atom representing _heuristic(...)
    Atom_t heuPred = makeAtom(cond, true);
    data_->addHeuristic(a, t, bias, prio, heuPred);
}
void SmodelsConvert::acycEdge(int s, int t, const LitSpan& condition) {
    if (not ext_) {
        out_.acycEdge(s, t, condition);
    }
    data_->output.emplace_back(makeAtom(condition, true), s, t);
}

void SmodelsConvert::flush() {
    flushMinimize();
    flushExternal();
    flushHeuristic();
    flushSymbols();
    auto f = -static_cast<Lit_t>(SmData::false_atom);
    out_.assume(toSpan(f));
    data_->flushStep();
}
void SmodelsConvert::endStep() {
    flush();
    out_.endStep();
}
void SmodelsConvert::flushMinimize() {
    if (data_->minimize.empty()) {
        return;
    }
    std::ranges::sort(data_->minimize, [](const auto& lhs, const auto& rhs) {
        return lhs.prio < rhs.prio || (lhs.prio == rhs.prio && lhs.startPos < rhs.startPos);
    });
    const SmData::Minimize* last = &data_->minimize[0];
    data_->rule.startMinimize(last->prio);
    for (const auto& m : data_->minimize) {
        if (last->prio != m.prio) {
            data_->rule.end(&out_);
            data_->rule.clear().startMinimize(m.prio);
            last = &m;
        }
        data_->mapBody(WeightLitSpan{data_->minLits.data() + m.startPos, m.endPos - m.startPos});
    }
    data_->rule.end(&out_);
}
void SmodelsConvert::flushExternal() {
    LitSpan trueBody{};
    data_->rule.clear();
    for (auto ext : data_->external) {
        SmData::Atom& a  = data_->mapAtom(ext);
        auto          vt = static_cast<TruthValue>(a.extn);
        if (not ext_) {
            if (a.head) {
                continue;
            }
            Atom_t at = a.sm();
            if (vt == TruthValue::free) {
                data_->rule.addHead(at);
            }
            else if (vt == TruthValue::true_) {
                out_.rule(HeadType::disjunctive, toSpan(at), trueBody);
            }
        }
        else {
            out_.external(a.sm(), vt);
        }
    }
    if (auto head = data_->rule.head(); not head.empty()) {
        out_.rule(HeadType::choice, head, trueBody);
    }
}
void SmodelsConvert::flushHeuristic() {
    if (data_->heuristic.empty()) {
        return;
    }
    SmData::ScratchType scratch;
    for (const auto& heu : data_->heuristic) {
        if (not data_->mapped(heu.atom)) {
            continue;
        }
        SmData::Atom& ma = data_->mapAtom(heu.atom);
        auto          it = ma.show ? data_->symTab.find(ma.smId) : data_->symTab.end();
        if (it == data_->symTab.end()) {
            ma.show = 1;
            it      = data_->addOutput(ma.sm(), ma.makePred(scratch));
        }
        auto c = static_cast<Lit_t>(heu.cond);
        out_.output(heu.makePred(scratch, it->second.view()), toSpan(c));
    }
}
void SmodelsConvert::flushSymbols() {
    SmData::ScratchType scratch;
    std::ranges::sort(data_->output, std::less{}, [](const SmData::Output& o) { return o.atom; });
    for (const auto& sym : data_->output) {
        auto x = static_cast<Lit_t>(sym.atom);
        out_.output(sym.makePred(scratch), toSpan(x));
    }
}
} // namespace Potassco
