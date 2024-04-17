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

#include <potassco/smodels.h>

#include <potassco/error.h>

POTASSCO_WARNING_BEGIN_RELAXED
#include <amc/vector.hpp>
POTASSCO_WARNING_END_RELAXED

#include <algorithm>
#include <charconv>
#include <memory>
#include <string_view>
#include <unordered_map>

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
namespace Potassco {
using namespace std::literals;
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert::SmData
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsConvert::SmData {
    using ScratchType = amc::SmallVector<char, 64>;
    template <std::integral T>
    static void append(ScratchType& scratch, T in) {
        char tmp[std::numeric_limits<T>::digits10 + 1];
        auto sz = std::to_chars(tmp, std::end(tmp), in).ptr - tmp;
        scratch.insert(scratch.end(), tmp, tmp + static_cast<std::size_t>(sz));
    }
    static void append(ScratchType& scratch, std::string_view n) { scratch.insert(scratch.end(), n.begin(), n.end()); }
    template <typename... Args>
    static std::string_view makePred(ScratchType& scratch, std::string_view name, Args... args) {
        static_assert(sizeof...(Args) > 0, "at least one arg expected");
        scratch.clear();
        scratch.insert(scratch.end(), name.begin(), name.end());
        scratch.push_back('(');
        ((append(scratch, args), scratch.push_back(',')), ...);
        scratch.back() = ')';
        return std::string_view{scratch.data(), scratch.size()};
    }
    struct Atom {
        Atom() : smId(0), head(0), show(0), extn(0) {}
        std::string_view makePred(ScratchType& scratch) const { return SmData::makePred(scratch, "_atom"sv, smId); }
        operator Atom_t() const { return smId; }
        uint32_t smId : 28; // corresponding smodels atom
        uint32_t head : 1;  // atom occurs in a head of a rule
        uint32_t show : 1;  // atom has a name
        uint32_t extn : 2;  // value if atom is external
    };
    struct Heuristic {
        std::string_view makePred(ScratchType& scratch, std::string_view atomName) const {
            return SmData::makePred(scratch, "_heuristic"sv, atomName, enum_name(type), bias, prio);
        }
        Atom_t      atom;
        Heuristic_t type;
        int         bias{};
        unsigned    prio{};
        unsigned    cond{};
    };
    struct Output {
        enum Type : uint8_t { Name = 0, Edge = 1 };
        struct EdgeT {
            int32_t s;
            int32_t t;
        };
        Output(Atom_t a = 0, const char* n = nullptr) : atom(a), type(Name), name(n) {}
        Output(Atom_t a, int32_t s, int32_t t) : atom(a), type(Edge), edge{s, t} {}
        std::string_view makePred(ScratchType& scratch) const {
            if (type == Name)
                return name;
            else
                return SmData::makePred(scratch, "_edge"sv, edge.s, edge.t);
        }
        uint32_t atom : 31;
        uint32_t type : 1;
        union {
            const char* name{};
            EdgeT       edge;
        };
    };
    static_assert(amc::is_trivially_relocatable_v<Atom> && amc::is_trivially_relocatable_v<Atom_t> &&
                  amc::is_trivially_relocatable_v<Lit_t> && amc::is_trivially_relocatable_v<WeightLit_t> &&
                  amc::is_trivially_relocatable_v<Heuristic> && amc::is_trivially_relocatable_v<Output>);
    using AtomMap = amc::vector<Atom>;
    using AtomVec = amc::vector<Atom_t>;
    using LitVec  = amc::vector<Lit_t>;
    using WLitVec = amc::vector<WeightLit_t>;
    using HeuVec  = amc::vector<Heuristic>;
    using OutVec  = amc::vector<Output>;
    using SymTab  = std::unordered_map<Atom_t, std::unique_ptr<char[]>>;
    struct Minimize {
        static_assert(amc::is_trivially_relocatable_v<WLitVec>);
        using trivially_relocatable = std::true_type;
        Weight_t prio;
        WLitVec  lits;
    };
    static_assert(amc::is_trivially_relocatable_v<Minimize>);
    using MinSet = amc::vector<Minimize>;
    SmData() : next_(2) {}
    Atom_t newAtom() { return next_++; }
    Atom_t falseAtom() { return 1; }
    bool   mapped(Atom_t a) const { return a < atoms_.size() && atoms_[a].smId != 0; }
    Atom&  mapAtom(Atom_t a) {
        if (mapped(a)) {
            return atoms_[a];
        }
        if (a >= atoms_.size()) {
            atoms_.resize(a + 1);
        }
        atoms_[a].smId = next_++;
        return atoms_[a];
    }
    Lit_t mapLit(Lit_t in) {
        auto x = static_cast<Lit_t>(mapAtom(atom(in)));
        return in < 0 ? -x : x;
    }
    WeightLit_t mapLit(WeightLit_t in) {
        in.lit = mapLit(in.lit);
        return in;
    }
    Atom_t mapHeadAtom(Atom_t a) {
        Atom& x = mapAtom(a);
        x.head  = 1;
        return x;
    }
    AtomSpan mapHead(const AtomSpan& h);
    template <class T>
    auto mapLits(const std::span<const T>& in, amc::vector<T>& out) -> std::span<const T> {
        out.clear();
        for (const auto& x : in) { out.push_back(mapLit(x)); }
        return out;
    }
    const char*               addOutput(Atom_t atom, const std::string_view&);
    [[nodiscard]] const char* getName(const Atom& atom) const {
        if (not atom.show)
            return nullptr;
        auto it = symTab_.find(atom);
        return it != symTab_.end() ? it->second.get() : nullptr;
    }
    void addMinimize(Weight_t prio, const WeightLitSpan& lits) {
        auto it = std::lower_bound(minimize_.begin(), minimize_.end(), prio,
                                   [](const Minimize& lhs, Weight_t rhs) { return lhs.prio < rhs; });
        if (it == minimize_.end() || it->prio != prio)
            it = minimize_.insert(it, {.prio = prio, .lits = {}});
        for (auto x : lits) {
            if (weight(x) < 0) {
                x.lit    = -x.lit;
                x.weight = -x.weight;
            }
            it->lits.push_back(x);
        }
    }
    void addExternal(Atom_t a, Value_t v) {
        if (auto& ma = mapAtom(a); not ma.head) {
            ma.extn = static_cast<unsigned>(v);
            extern_.push_back(a);
        }
    }
    void addHeuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, Atom_t cond) {
        Heuristic h = {a, t, bias, prio, cond};
        heuristic_.push_back(h);
    }
    void flushStep() {
        minimize_.clear();
        AtomVec().swap(extern_);
        HeuVec().swap(heuristic_);
        output_.clear();
    }
    AtomMap atoms_;     // maps input atoms to output atoms
    SymTab  symTab_;    // maps output atoms to their names
    AtomVec head_;      // active rule head
    LitVec  lits_;      // active body literals
    WLitVec wlits_;     // active weight body literals
    AtomVec extern_;    // external atoms
    HeuVec  heuristic_; // list of heuristic modifications not yet processed
    MinSet  minimize_;  // set of minimize literals
    OutVec  output_;    // list of output atoms not yet processed
    Atom_t  next_;      // next unused output atom
};
AtomSpan SmodelsConvert::SmData::mapHead(const AtomSpan& h) {
    head_.clear();
    for (auto a : h) { head_.push_back(mapHeadAtom(a)); }
    if (head_.empty()) {
        head_.push_back(falseAtom());
    }
    return head_;
}
const char* SmodelsConvert::SmData::addOutput(Atom_t atom, const std::string_view& str) {
    auto name                                      = std::make_unique<char[]>(str.size() + 1);
    *std::copy(str.begin(), str.end(), name.get()) = 0;

    const char* n    = name.get();
    auto [it, added] = symTab_.emplace(atom, std::move(name));
    POTASSCO_CHECK_PRE(added, "Redefinition: atom '%u:%s' already shown as '%s'", atom, n, it->second.get());
    output_.emplace_back(atom, n);
    return n;
}
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsConvert
/////////////////////////////////////////////////////////////////////////////////////////
SmodelsConvert::SmodelsConvert(AbstractProgram& out, bool ext) : out_(out), data_(new SmData), ext_(ext) {}
SmodelsConvert::~SmodelsConvert() { delete data_; }
Lit_t    SmodelsConvert::get(Lit_t in) const { return data_->mapLit(in); }
unsigned SmodelsConvert::maxAtom() const { return data_->next_ - 1; }
Atom_t   SmodelsConvert::makeAtom(const LitSpan& cond, bool named) {
    Atom_t id = 0;
    if (cond.size() != 1 || cond[0] < 0 || (data_->mapAtom(atom(cond[0])).show && named)) {
        // aux :- cond.
        Atom_t aux = (id = data_->newAtom());
        out_.rule(Head_t::Disjunctive, {&aux, 1}, data_->mapLits(cond, data_->lits_));
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
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    if (not head.empty() || ht == Head_t::Disjunctive) {
        AtomSpan mHead = data_->mapHead(head);
        out_.rule(ht, mHead, data_->mapLits(body, data_->lits_));
    }
}
void SmodelsConvert::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (not head.empty() || ht == Head_t::Disjunctive) {
        AtomSpan      mHead = data_->mapHead(head);
        WeightLitSpan mBody = data_->mapLits(body, data_->wlits_);
        if (isSmodelsRule(ht, mHead, bound, mBody)) {
            out_.rule(ht, mHead, bound, mBody);
            return;
        }
        Atom_t aux = data_->newAtom();
        data_->lits_.assign(1, lit(aux));
        out_.rule(Head_t::Disjunctive, {&aux, 1}, bound, mBody);
        out_.rule(ht, mHead, data_->lits_);
    }
}

void SmodelsConvert::minimize(Weight_t prio, const WeightLitSpan& lits) { data_->addMinimize(prio, lits); }
void SmodelsConvert::output(const std::string_view& str, const LitSpan& cond) {
    // create a unique atom for cond and set its name to str
    data_->addOutput(makeAtom(cond, true), str);
}

void SmodelsConvert::external(Atom_t a, Value_t v) { data_->addExternal(a, v); }
void SmodelsConvert::heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& cond) {
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
    data_->output_.emplace_back(makeAtom(condition, true), s, t);
}

void SmodelsConvert::flush() {
    flushMinimize();
    flushExternal();
    flushHeuristic();
    flushSymbols();
    Lit_t f = -static_cast<Lit_t>(data_->falseAtom());
    out_.assume({&f, 1});
    data_->flushStep();
}
void SmodelsConvert::endStep() {
    flush();
    out_.endStep();
}
void SmodelsConvert::flushMinimize() {
    const SmData::Minimize* last = nullptr;
    for (const auto& m : data_->minimize_) {
        assert(not last || last->prio < m.prio);
        out_.minimize(m.prio, data_->mapLits(std::span{m.lits}, data_->wlits_));
        last = &m;
    }
}
void SmodelsConvert::flushExternal() {
    LitSpan T{};
    data_->head_.clear();
    for (auto ext : data_->extern_) {
        SmData::Atom& a  = data_->mapAtom(ext);
        auto          vt = static_cast<Value_t>(a.extn);
        if (not ext_) {
            if (a.head) {
                continue;
            }
            Atom_t at = a;
            if (vt == Value_t::Free) {
                data_->head_.push_back(at);
            }
            else if (vt == Value_t::True) {
                out_.rule(Head_t::Disjunctive, {&at, 1}, T);
            }
        }
        else {
            out_.external(a, vt);
        }
    }
    if (not data_->head_.empty()) {
        out_.rule(Head_t::Choice, data_->head_, T);
    }
}
void SmodelsConvert::flushHeuristic() {
    if (data_->heuristic_.empty())
        return;
    amc::SmallVector<char, 64> scratch;
    for (const auto& heu : data_->heuristic_) {
        if (not data_->mapped(heu.atom)) {
            continue;
        }
        SmData::Atom& ma   = data_->mapAtom(heu.atom);
        const char*   name = data_->getName(ma);
        if (not name) {
            ma.show = 1;
            name    = data_->addOutput(ma, ma.makePred(scratch));
        }
        auto c = static_cast<Lit_t>(heu.cond);
        out_.output(heu.makePred(scratch, name), {&c, 1});
    }
}
void SmodelsConvert::flushSymbols() {
    amc::SmallVector<char, 64> scratch;
    std::stable_sort(data_->output_.begin(), data_->output_.end(),
                     [](const auto& lhs, const auto& rhs) { return lhs.atom < rhs.atom; });
    for (const auto& sym : data_->output_) {
        auto x = static_cast<Lit_t>(sym.atom);
        out_.output(sym.makePred(scratch), {&x, 1});
    }
}
} // namespace Potassco
