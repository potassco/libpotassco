//
// Copyright (c) 2017 - 2025, Roland Kaminski
// Copyright (c) 2025 - present, Francois Laferriere, Benjamin Kaufmann
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
#include <potassco/utils.h>

#include <algorithm>
#include <ostream>
#include <ranges>
#include <tuple>
#include <vector>

namespace Potassco {
namespace {
/////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////
constexpr auto headT(HeadType ht, std::size_t id) -> std::tuple<const char*, std::size_t> {
    return std::make_tuple(ht == HeadType::disjunctive ? "disjunction" : "choice", id);
}
constexpr auto normalT(std::size_t id) -> std::tuple<const char*, std::size_t> { return std::make_tuple("normal", id); }
constexpr auto sumT(std::size_t id, Weight_t bound) -> std::tuple<const char*, std::size_t, Weight_t> {
    return std::make_tuple("sum", id, bound);
}
struct Quoted {
    std::string_view str;
};
template <typename T, typename... V>
void printCommaSeparated(std::ostream& out, const T& t, const V&... v);

template <typename T>
void printValue(std::ostream& out, const T& value) {
    out << value;
}

template <typename... Args>
void printValue(std::ostream& out, const std::tuple<const char*, Args...>& value) {
    std::apply(
        [&](const char* name, const auto&... args) {
            out << name << "(";
            printCommaSeparated(out, args...);
            out << ")";
        },
        value);
}

void printValue(std::ostream& out, const WeightLit& value) { printCommaSeparated(out, value.lit, value.weight); }

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
} // end unnamed namespace
/////////////////////////////////////////////////////////////////////////////////////////
// Reifier
/////////////////////////////////////////////////////////////////////////////////////////
struct Reifier::StepData {
    static constexpr auto hash(std::integral auto x) -> uint32_t { return hashId(static_cast<uint32_t>(x)); }
    static constexpr auto hash(WeightLit x) -> uint32_t { return hash(x.lit) + hash(x.weight); }
    using TupleData = std::vector<std::byte>;
    using Elements  = std::vector<std::size_t>;
    template <typename T>
    static auto pushTuple(TupleData& target, std::span<const T> tuple) -> std::span<T> {
        auto start = target.size();
        auto bytes = as_bytes(tuple);
        auto size  = static_cast<uint32_t>(tuple.size());
        target.insert(target.end(), reinterpret_cast<const std::byte*>(&size),
                      reinterpret_cast<const std::byte*>(&size) + sizeof(uint32_t));
        target.insert(target.end(), bytes.data(), bytes.data() + bytes.size());
        return std::span{reinterpret_cast<T*>(target.data() + start + sizeof(uint32_t)), tuple.size()};
    }
    template <typename T>
    static auto getTuple(const TupleData& source, std::size_t start) -> std::span<const T> {
        POTASSCO_ASSERT(start < source.size());
        auto* data = source.data() + start;
        auto  sz   = *reinterpret_cast<const uint32_t*>(data);
        return std::span{reinterpret_cast<const T*>(data + sizeof(uint32_t)), sz};
    }

    struct TupleSet {
        explicit TupleSet(const char* n) : name(n) {}
        [[nodiscard]] constexpr auto get(uint32_t tId) const -> std::size_t { return elements.at(tId); }
        auto                         add(std::size_t pos, DynamicIndex::IndexRef r, uint32_t hash) -> uint32_t {
            auto nId = static_cast<uint32_t>(elements.size());
            POTASSCO_CHECK_PRE(nId < UINT32_MAX, "too many %s tuples", name);
            elements.push_back(pos);
            index.add(r, hash, nId);
            return nId;
        }
        const char*  name{nullptr};
        Elements     elements;
        DynamicIndex index;
    };

    template <typename C, typename T>
    auto addTuple(Reifier& self, C& ts, std::span<const T> arg, bool canonical = true) -> uint32_t {
        static_assert(alignof(T) <= alignof(uint32_t));
        POTASSCO_CHECK_PRE(arg.size() < UINT32_MAX, "%s tuple too large", ts.name);
        auto start = tupleData.size();
        auto dirty = not canonical;
        if (canonical) {
            auto scratch = pushTuple(tupleData, arg);
            std::ranges::sort(scratch);
            if (auto rem = std::ranges::size(std::ranges::unique(scratch)); rem > 0) {
                scratch = scratch.first(scratch.size() - rem);
                dirty   = true;
            }
            arg = scratch;
        }
        auto abst = 0u;
        for (auto x : arg) { abst += hash(x); }
        auto r = ts.index.find_if(abst,
                                  [&](Id_t id) { return std::ranges::equal(arg, getTuple<T>(tupleData, ts.get(id))); });
        if (not r) {
            auto nId = ts.add(start, r, abst);
            if (dirty) {
                tupleData.resize(start);
                pushTuple(tupleData, arg);
            }
            self.printFact(ts.name, nId);
            if (canonical) {
                for (const auto& x : arg) { self.printFact(ts.name, nId, x); }
            }
            else {
                for (auto [idx, x] : enumerate(arg)) { self.printFact(ts.name, nId, idx, x); }
            }
            return nId;
        }
        tupleData.resize(start);
        return *r;
    }
    Id_t addNode(Atom_t atom) {
        auto r = nodes.find_if(atom, [&](Id_t nId) { return graph.getData(nId) == atom; });
        if (not r) {
            auto id = graph.addNode(atom);
            nodes.add(r, atom, id);
            return id;
        }
        return *r;
    }
    template <typename T>
    void addPositiveEdges(const AtomSpan& head, const std::span<const T>& body) {
        for (auto atom : head) {
            auto uId = addNode(atom);
            for (const auto& elem : body) {
                if (lit(elem) > 0) {
                    auto vId = addNode(Potassco::atom(elem));
                    graph.addEdge(uId, vId);
                }
            }
        }
    }

    TupleData tupleData;
    TupleSet  theoryTuples{"theory_tuple"};
    TupleSet  theoryElementTuples{"theory_element_tuple"};
    TupleSet  litTuples{"literal_tuple"};
    TupleSet  atomTuples{"atom_tuple"};
    TupleSet  weightLitTuples{"weighted_literal_tuple"};

    Graph<Atom_t> graph;
    DynamicIndex  nodes;
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
    if (step_) {
        out_ << "," << step_ - 1;
    }
    out_ << ").\n";
}

auto Reifier::theoryTuple(IdSpan args) -> size_t {
    return stepData_->addTuple(*this, stepData_->theoryTuples, args, false);
}

auto Reifier::theoryElementTuple(IdSpan args) -> size_t {
    return stepData_->addTuple(*this, stepData_->theoryElementTuples, args);
}

auto Reifier::litTuple(LitSpan args) -> size_t { return stepData_->addTuple(*this, stepData_->litTuples, args); }

auto Reifier::weightLitTuple(WeightLitSpan args) -> size_t {
    return stepData_->addTuple(*this, stepData_->weightLitTuples, args);
}

auto Reifier::atomTuple(AtomSpan args) -> size_t { return stepData_->addTuple(*this, stepData_->atomTuples, args); }

void Reifier::initProgram(bool incremental) {
    if (incremental) {
        printFact("tag", "incremental");
    }
}

void Reifier::beginStep() {
    if (reifyStep_) {
        ++step_;
    }
}

void Reifier::rule(HeadType ht, AtomSpan head, LitSpan body) {
    auto headId = atomTuple(head);
    auto bodyId = litTuple(body);
    printFact("rule", headT(ht, headId), normalT(bodyId));
    if (calculateSccs_) {
        stepData_->addPositiveEdges(head, body);
    }
}

void Reifier::rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) {
    auto headId = atomTuple(head);
    auto bodyId = weightLitTuple(body);
    printFact("rule", headT(ht, headId), sumT(bodyId, bound));
    if (calculateSccs_) {
        stepData_->addPositiveEdges(head, body);
    }
}

void Reifier::minimize(Weight_t prio, WeightLitSpan lits) { printFact("minimize", prio, weightLitTuple(lits)); }

void Reifier::project(AtomSpan atoms) {
    for (const auto& x : atoms) { printFact("project", x); }
}

void Reifier::outputAtom(Atom_t atom, std::string_view name) { printFact("outputAtom", name, atom); }

void Reifier::outputTerm(Id_t termId, std::string_view name) { printFact("outputTerm", name, termId); }

void Reifier::output(Id_t termId, LitSpan condition) { printFact("output", termId, litTuple(condition)); }

void Reifier::external(Atom_t a, TruthValue v) { printFact("external", a, enum_name(v)); }

void Reifier::assume(LitSpan lits) {
    for (const auto& x : lits) { printFact("assume", x); }
}

void Reifier::heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) {
    printFact("heuristic", a, enum_name(t), bias, prio, litTuple(condition));
}

void Reifier::acycEdge(int s, int t, LitSpan condition) { printFact("edge", s, t, litTuple(condition)); }

void Reifier::theoryTerm(Id_t termId, int number) { printFact("theory_number", termId, number); }

void Reifier::theoryTerm(Id_t termId, std::string_view name) { printFact("theory_string", termId, Quoted{name}); }

void Reifier::theoryTerm(Id_t termId, int cId, IdSpan args) {
    auto tId = theoryTuple(args);
    if (cId >= 0) {
        printFact("theory_function", termId, cId, tId);
    }
    else {
        const char* type;
        switch (cId) {
            case -1: type = "tuple"; break;
            case -2: type = "set"; break;
            case -3: type = "list"; break;
            default: POTASSCO_ASSERT_NOT_REACHED("unexpected tuple type");
        }
        printFact("theory_sequence", termId, type, tId);
    }
}

void Reifier::theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) {
    auto tt = theoryTuple(terms);
    auto lt = litTuple(cond);
    printFact("theory_element", elementId, tt, lt);
}

void Reifier::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) {
    printFact("theory_atom", atomOrZero, termId, theoryElementTuple(elements));
}

void Reifier::theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) {
    printFact("theory_atom", atomOrZero, termId, theoryElementTuple(elements), op, rhs);
}

void Reifier::endStep() {
    for (auto [i, scc] : enumerate(stepData_->graph.computeNonTrivialSccs())) {
        for (auto x : std::views::reverse(scc)) { printFact("scc", i, x); }
    }
    if (reifyStep_) {
        std::exchange(stepData_, std::make_unique<StepData>()).reset();
    }
}

void Reifier::parse(std::istream& in) { readAspif(in, *this); }

} // namespace Potassco
