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
#include <potassco/format.h>
#include <potassco/rule_utils.h>

#include <algorithm>
#include <memory>
#include <string_view>

namespace Potassco {
using namespace std::literals;
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert::SmData
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsConvert::SmData {
    using ScratchType = BasicCharBuffer;
    template <typename... Args>
    static std::string_view makePred(ScratchType& buffer, std::string_view name, const Args&... args) {
        static_assert(sizeof...(Args) > 0, "at least one arg expected");
        buffer.clear();
        buffer.append(name).append("("sv);
        buffer.appendSep(",", args...);
        buffer.push_back(')');
        return buffer.view();
    }
    struct Atom {
        [[nodiscard]] auto makePred(ScratchType& buffer) const -> std::string_view {
            return SmData::makePred(buffer, "_atom"sv, smId);
        }
        [[nodiscard]] auto sm() const -> Atom_t { return smId; }
        [[nodiscard]] bool hasName() const { return name != id_max; }

        Id_t     name{id_max};  // (optional) name of smodels (output) atom, i.e., symbol table entry
        uint32_t smId : 28 {0}; // corresponding smodels (output) atom
        uint32_t head : 1 {0};  // atom occurs in a head of a rule
        uint32_t show : 1 {0};  // atom has a name
        uint32_t extn : 2 {0};  // value if atom is external
    };
    struct Heuristic {
        std::string_view makePred(ScratchType& buffer, std::string_view atomName) const {
            return SmData::makePred(buffer, "_heuristic"sv, atomName, enum_name(type), bias, prio);
        }
        Atom_t      atom;
        DomModifier type;
        int         bias{};
        unsigned    prio{};
        unsigned    cond{};
    };
    struct OutTerm {
        OutTerm() = default;
        explicit OutTerm(Id_t str) : name(str) {}
        void step() {
            if (auto prev = std::exchange(atom, 0u); prev) {
                last = prev;
            }
        }
        Atom_t atom{0u};
        Atom_t last{0u};
        Id_t   name{id_max};
    };
    struct Output {
        enum Type : uint8_t { type_name = 0, type_edge = 1 };
        explicit Output(const Atom& a) : atom(a.sm()), type(type_name), name(a.name), end(0) {}
        explicit Output(const OutTerm& term) : atom(term.atom), type(type_name), name(term.name), end(1) {}
        Output(Atom_t a, int32_t s, int32_t t) : atom(a), type(type_edge), start(s), end(t) {}
        auto makePred(SmData& self) const -> std::string_view {
            if (type == type_edge) {
                return SmData::makePred(self.scratch, "_edge"sv, start, end);
            }
            auto& n = self.strings[name];
            return end == 0 ? n.view() : SmData::makePred(self.scratch, "_show_term"sv, n.view());
        }
        uint32_t atom : 31;
        uint32_t type : 1;
        union {
            Id_t    name;
            int32_t start;
        };
        int32_t end;
    };
    using AtomMap = Vector<Atom>;
    using AtomVec = Vector<Atom_t>;
    using WLitVec = Vector<WeightLit>;
    using HeuVec  = Vector<Heuristic>;
    using OutVec  = Vector<Output>;
    using TermVec = Vector<OutTerm>;
    struct Minimize {
        Weight_t prio;
        unsigned startPos;
        unsigned endPos;
    };
    static constexpr Atom_t false_atom = 1;
    using MinSet                       = Vector<Minimize>;
    SmData()                           = default;
    [[nodiscard]] auto mapped(Atom_t a) -> Atom* {
        return a < atoms.size() && atoms[a].smId != 0 ? &atoms[a] : nullptr;
    }
    //
    auto newAtom() -> Atom_t { return next++; }
    auto mapAtom(Atom_t a) -> Atom& {
        if (auto* ma = mapped(a); ma != nullptr) {
            return *ma;
        }
        if (a >= atoms.size()) {
            atoms.resize(a + 1);
        }
        atoms[a].smId = next++;
        return atoms[a];
    }
    auto mapLit(Lit_t in) -> Lit_t {
        auto x = static_cast<Lit_t>(mapAtom(atom(in)).sm());
        return in < 0 ? -x : x;
    }
    auto mapLit(WeightLit in) -> WeightLit {
        in.lit = mapLit(in.lit);
        return in;
    }
    auto mapHeadAtom(Atom_t a) -> Atom_t {
        Atom& x = mapAtom(a);
        x.head  = 1;
        return x.sm();
    }
    auto mapHead(AtomSpan h, HeadType ht = HeadType::disjunctive) -> RuleBuilder& {
        rule.clear().start(ht);
        for (auto a : h) { rule.addHead(mapHeadAtom(a)); }
        if (h.empty()) {
            rule.addHead(false_atom);
        }
        return rule;
    }
    template <class T>
    auto mapBody(std::span<const T> in) -> RuleBuilder& {
        for (const auto& x : in) { rule.addGoal(mapLit(x)); }
        return rule;
    }
    void addOutput(Atom& atom, std::string_view str) {
        POTASSCO_CHECK_PRE(not atom.hasName(), "Redefinition: atom '%u:%" PRIsv "' already shown as '%s'", atom.sm(),
                           PRI_SV(str), strings[atom.name].c_str());
        atom.show = 1u;
        atom.name = strings.add(str).first;
        output.emplace_back(atom);
    }
    void addTerm(Id_t termId, std::string_view str) {
        if (termId >= terms.size()) {
            terms.resize(termId + 1);
        }
        auto& t    = terms[termId];
        auto  name = strings.add(str).first;
        POTASSCO_CHECK_PRE(t.name == id_max || t.name == name,
                           "Redefinition: term '%u:%" PRIsv "' already defined as '%s'", termId, PRI_SV(str),
                           strings[t.name].c_str());
        t.name = name;
    }
    void addMinimize(Weight_t prio, WeightLitSpan lits) {
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
        reset(minimize);
        reset(minLits);
        reset(external);
        reset(heuristic);
        output.clear();
    }

    BasicCharBuffer  scratch;   // scratch buffer
    OrderedStringSet strings;   // set of strings
    AtomMap          atoms;     // maps input atoms to output atoms
    TermVec          terms;     // list of output terms
    AtomVec          external;  // external atoms
    HeuVec           heuristic; // list of heuristic modifications not yet processed
    MinSet           minimize;  // set of minimize constraints
    WLitVec          minLits;   // minimize literals
    OutVec           output;    // list of output atoms not yet processed
    RuleBuilder      rule;      // active (mapped) rule
    Atom_t           next{2};   // next unused output atom
};
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert
/////////////////////////////////////////////////////////////////////////////////////////
SmodelsConvert::SmodelsConvert(AbstractProgram& out, bool ext)
    : out_(out)
    , data_(std::make_unique<SmData>())
    , ext_(ext) {}
SmodelsConvert::~SmodelsConvert() = default;
auto SmodelsConvert::get(Lit_t in) const -> Lit_t { return data_->mapLit(in); }
auto SmodelsConvert::maxAtom() const -> Atom_t { return data_->next - 1; }
auto SmodelsConvert::makeAtom(LitSpan lits, Lit_t last, bool named) -> Atom_t {
    auto   sz    = lits.size() + static_cast<uint32_t>(last != 0);
    auto   front = lits.empty() ? last : lits.front();
    Atom_t id;
    if (sz != 1 || front <= 0 || (data_->mapAtom(atom(front)).show && named)) {
        // aux :- lits [, last]
        data_->rule.clear().addHead(id = data_->newAtom());
        auto& r = data_->mapBody(lits);
        if (last) {
            r.addGoal(last);
        }
        r.end(&out_);
    }
    else {
        auto& ma = data_->mapAtom(atom(front));
        ma.show  = static_cast<unsigned>(named);
        id       = ma.smId;
    }
    return id;
}
void SmodelsConvert::initProgram(bool inc) { out_.initProgram(inc); }
void SmodelsConvert::beginStep() {
    out_.beginStep();
    for (auto& t : data_->terms) { t.step(); }
}
void SmodelsConvert::rule(HeadType ht, AtomSpan head, LitSpan body) {
    if (not head.empty() || ht == HeadType::disjunctive) {
        data_->mapHead(head, ht).startBody();
        data_->mapBody(body).end(&out_);
    }
}
void SmodelsConvert::rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) {
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

void SmodelsConvert::minimize(Weight_t prio, WeightLitSpan lits) { data_->addMinimize(prio, lits); }
void SmodelsConvert::outputAtom(Atom_t atom, std::string_view name) {
    POTASSCO_CHECK_PRE(atom, "atom expected");
    data_->addOutput(data_->mapAtom(Potassco::atom(atom)), name);
}
void SmodelsConvert::outputTerm(Id_t termId, std::string_view name) { data_->addTerm(termId, name); }
void SmodelsConvert::output(Id_t termId, LitSpan cond) {
    auto* term = termId < data_->terms.size() ? &data_->terms[termId] : nullptr;
    POTASSCO_CHECK_PRE(term != nullptr && term->name != id_max, "Undefined: term %u is unknown", termId);
    auto condAtom = makeAtom(cond, neg(term->last), false);
    if (not term->atom) {
        term->atom = data_->newAtom();
        data_->output.emplace_back(*term);
    }
    data_->rule.clear().addHead(term->atom).addGoal(lit(condAtom)).end(&out_);
}
void SmodelsConvert::external(Atom_t a, TruthValue v) { data_->addExternal(a, v); }
void SmodelsConvert::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan cond) {
    if (not ext_) {
        out_.heuristic(a, t, bias, prio, cond);
    }
    // create unique atom representing _heuristic(...)
    Atom_t heuPred = makeAtom(cond, 0, true);
    data_->addHeuristic(a, t, bias, prio, heuPred);
}
void SmodelsConvert::acycEdge(int s, int t, LitSpan condition) {
    if (not ext_) {
        out_.acycEdge(s, t, condition);
    }
    data_->output.emplace_back(makeAtom(condition, 0, true), s, t);
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
    const auto* last = data_->minimize.data();
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
        const auto& a  = data_->mapAtom(ext);
        auto        vt = static_cast<TruthValue>(a.extn);
        if (not ext_) {
            if (a.head) {
                continue;
            }
            if (auto at = a.sm(); vt == TruthValue::free) {
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
    for (const auto& heu : data_->heuristic) {
        if (auto* ma = data_->mapped(heu.atom); ma != nullptr) {
            if (not ma->hasName()) {
                data_->addOutput(*ma, ma->makePred(data_->scratch));
                assert(ma->hasName());
            }
            out_.outputAtom(heu.cond, heu.makePred(data_->scratch, data_->strings[ma->name].view()));
        }
    }
}
void SmodelsConvert::flushSymbols() {
    if (not data_->output.empty()) {
        radixSort(data_->output, [](const SmData::Output& o) { return o.atom; }, radix_relaxed);
        for (const auto& sym : data_->output) { out_.outputAtom(sym.atom, sym.makePred(*data_)); }
    }
}
} // namespace Potassco
