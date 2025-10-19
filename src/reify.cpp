//
// Copyright (c) 2017 - 2025, Roland Kaminski
// Copyright (c) 2025 - present, Francois Laferriere
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
#include <potassco/enum.h>
#include <potassco/error.h>
#include <potassco/graph.h>
#include <potassco/reify.h>

#include <algorithm>
#include <ostream>
#include <ranges>
#include <unordered_map>
#include <vector>

namespace Potassco {
namespace {
/////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////
struct Head {
    HeadType type;
    size_t   id;
};

struct Normal {
    size_t id;
};

struct Sum {
    size_t   id;
    Weight_t bound;
};

struct Quoted {
    std::string_view str;
};

template <typename T>
void printValue(std::ostream& out, const T& value) {
    out << value;
}

void printValue(std::ostream& out, const WeightLit& value) {
    printValue(out, value.lit);
    out << ",";
    printValue(out, value.weight);
}

void printValue(std::ostream& out, const Head& h) {
    const char* name = (h.type == HeadType::disjunctive ? "disjunction" : "choice");
    out << name << "(" << h.id << ")";
}

void printValue(std::ostream& out, const Normal& n) { out << "normal(" << n.id << ")"; }

void printValue(std::ostream& out, const Sum& s) { out << "sum(" << s.id << "," << s.bound << ")"; }

void printValue(std::ostream& out, const Quoted& q) {
    out.put('"');
    for (auto c : q.str) {
        switch (c) {
            case '\n': out << "\\n"; break;
            case '\\': out << "\\\\"; break;
            case '"' : out << "\\\""; break;
            default  : out.put(c); break;
        }
    }
    out.put('"');
}

template <typename T, typename... V>
void printCommaSeparated(std::ostream& out, const T& t, const V&... v) {
    printValue(out, t);
    ((out << ",", printValue(out, v)), ...);
}
template <typename T>
struct VectorHash {
    size_t operator()(const std::vector<T>& vec) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        auto* data = reinterpret_cast<const char*>(vec.data());
        return std::hash<std::string_view>{}({data, vec.size() * sizeof(T)});
    }
};

template <typename T>
std::vector<T> toVec(std::span<const T> span) {
    return {span.begin(), span.end()};
}

} // end unnamed namespace

/////////////////////////////////////////////////////////////////////////////////////////
// Reifier
/////////////////////////////////////////////////////////////////////////////////////////
template <typename T>
using SeqMap = std::unordered_map<std::vector<T>, size_t, VectorHash<T>>;

struct Reifier::StepData {
    SeqMap<Id_t>      theoryTuples;
    SeqMap<Id_t>      theoryElementTuples;
    SeqMap<Lit_t>     litTuples;
    SeqMap<Atom_t>    atomTuples;
    SeqMap<WeightLit> weightLitTuples;

    Graph<Atom_t>                        graph;
    std::unordered_map<Atom_t, uint32_t> nodes;

    void clear() {
        theoryTuples.clear();
        theoryElementTuples.clear();
        litTuples.clear();
        atomTuples.clear();
        weightLitTuples.clear();
        graph.clear();
        nodes.clear();
    }
};

Reifier::Reifier(std::ostream& out, const Options& opts)
    : out_(out)
    , stepData_(std::make_unique<StepData>())
    , calculateSccs_(opts.calculateSccs)
    , reifyStep_(opts.reifyStep) {}

Reifier::~Reifier() noexcept = default;

template <typename... T>
void Reifier::printFact(const char* name, const T&... args) {
    out_ << name << "(";
    printCommaSeparated(out_, args...);
    out_ << ").\n";
}

template <typename... T>
void Reifier::printStepFact(const char* name, const T&... args) {
    if (reifyStep_) {
        printFact(name, args..., step_);
    }
    else {
        printFact(name, args...);
    }
}

template <typename M, typename T>
auto Reifier::tuple(M& map, const char* name, std::span<T> args) -> size_t {
    auto owned = toVec(args);
    std::ranges::sort(owned);
    owned.erase(std::ranges::unique(owned).begin(), owned.end());
    auto [it, isNew] = map.emplace(std::move(owned), map.size());
    if (isNew) {
        printStepFact(name, it->second);
        for (const auto& x : it->first) { printStepFact(name, it->second, x); }
    }
    return it->second;
}

auto Reifier::theoryTuple(IdSpan args) -> size_t {
    auto& map        = stepData_->theoryTuples;
    auto  owned      = toVec(args);
    auto [it, isNew] = map.emplace(std::move(owned), map.size());
    if (isNew) {
        printStepFact("theory_tuple", it->second);
        int arg = 0;
        for (const auto& x : it->first) {
            printStepFact("theory_tuple", it->second, arg, x);
            ++arg;
        }
    }
    return it->second;
}

auto Reifier::theoryElementTuple(IdSpan args) -> size_t {
    return tuple(stepData_->theoryElementTuples, "theory_element_tuple", args);
}

auto Reifier::litTuple(LitSpan args) -> size_t { return tuple(stepData_->litTuples, "literal_tuple", args); }

auto Reifier::weightLitTuple(WeightLitSpan args) -> size_t {
    return tuple(stepData_->weightLitTuples, "weighted_literal_tuple", args);
}

auto Reifier::atomTuple(AtomSpan args) -> size_t { return tuple(stepData_->atomTuples, "atom_tuple", args); }

auto Reifier::addNode(Atom_t atom) -> uint32_t {
    auto [it, isNew] = stepData_->nodes.try_emplace(atom, 0);
    if (isNew) {
        it->second = stepData_->graph.addNode(atom);
    }
    return it->second;
}

void Reifier::initProgram(bool incremental) {
    if (incremental) {
        printFact("tag", "incremental");
    }
}

void Reifier::beginStep() {}

void Reifier::rule(HeadType ht, AtomSpan head, LitSpan body) {
    auto headId = atomTuple(head);
    auto bodyId = litTuple(body);
    printStepFact("rule", Head{ht, headId}, Normal{bodyId});
    if (calculateSccs_) {
        calculateSccs(head, body);
    }
}

void Reifier::rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) {
    auto headId = atomTuple(head);
    auto bodyId = weightLitTuple(body);
    printStepFact("rule", Head{ht, headId}, Sum{bodyId, bound});
    if (calculateSccs_) {
        calculateSccs(head, body);
    }
}

template <typename L>
void Reifier::calculateSccs(AtomSpan head, std::span<const L> body) {
    for (const auto& atom : head) {
        auto uId = addNode(atom);
        for (const auto& elem : body) {
            if (lit(elem) > 0) {
                auto vId = addNode(Potassco::atom(elem));
                stepData_->graph.addEdge(uId, vId);
            }
        }
    }
}

void Reifier::minimize(Weight_t prio, WeightLitSpan lits) { printStepFact("minimize", prio, weightLitTuple(lits)); }

void Reifier::project(AtomSpan atoms) {
    for (const auto& x : atoms) { printStepFact("project", x); }
}

void Reifier::outputAtom(Atom_t atom, std::string_view name) { printStepFact("outputAtom", name, atom); }

void Reifier::outputTerm(Id_t termId, std::string_view name) { printStepFact("outputTerm", name, termId); }

void Reifier::output(Id_t termId, LitSpan condition) { printStepFact("output", termId, litTuple(condition)); }

void Reifier::external(Atom_t a, TruthValue v) { printStepFact("external", a, enum_name(v)); }

void Reifier::assume(LitSpan lits) {
    for (const auto& x : lits) { printStepFact("assume", x); }
}

void Reifier::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) {
    printStepFact("heuristic", a, enum_name(t), bias, prio, litTuple(condition));
}

void Reifier::acycEdge(int s, int t, LitSpan condition) { printStepFact("edge", s, t, litTuple(condition)); }

void Reifier::theoryTerm(Id_t termId, int number) { printStepFact("theory_number", termId, number); }

void Reifier::theoryTerm(Id_t termId, std::string_view name) { printStepFact("theory_string", termId, Quoted{name}); }

void Reifier::theoryTerm(Id_t termId, int cId, IdSpan args) {
    if (cId >= 0) {
        printStepFact("theory_function", termId, cId, theoryTuple(args));
    }
    else {
        const char* type;
        switch (cId) {
            case -1: type = "tuple"; break;
            case -2: type = "set"; break;
            case -3: type = "list"; break;
            default: POTASSCO_ASSERT_NOT_REACHED("unexpected tuple type");
        }
        printStepFact("theory_sequence", termId, type, theoryTuple(args));
    }
}

void Reifier::theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) {
    auto tt = theoryTuple(terms);
    auto lt = litTuple(cond);
    printStepFact("theory_element", elementId, tt, lt);
}

void Reifier::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) {
    printStepFact("theory_atom", atomOrZero, termId, theoryElementTuple(elements));
}

void Reifier::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) {
    printStepFact("theory_atom", atomOrZero, termId, theoryElementTuple(elements), op, rhs);
}

void Reifier::endStep() {
    for (auto [i, scc] : enumerate(stepData_->graph.computeNonTrivialSccs())) {
        for (auto x : std::views::reverse(scc)) { printStepFact("scc", i, x); }
    }
    if (reifyStep_) {
        stepData_->clear();
        ++step_;
    }
}

void Reifier::parse(std::istream& in) { readAspif(in, *this); }

} // namespace Potassco
