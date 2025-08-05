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
#pragma once

#include <potassco/aspif.h>

#include <cstdint>

namespace Potassco {
//! Writes a program in reified facts format to the given output stream.
/*!
 * \ingroup WriteType
 */
class Reifier : public Potassco::AbstractProgram {
public:
    //! Options for configuring Reifier behavior.
    struct Options {
        bool calculateSccs = false; //!< Compute strongly connected components.
        bool reifyStep     = false; //!< Include step information in output.
    };
    //! Creates a new object and associates it with the given output stream.
    /*!
     * \param out Output stream to which program is written.
     * \param opts Options to configure Reifier behavior.
     */
    explicit Reifier(std::ostream& out, const Options& opts);
    ~Reifier() noexcept override;
    Reifier(const Reifier&)            = delete;
    Reifier& operator=(const Reifier&) = delete;

    //! Parses an aspif program and writes facts to the output stream.
    void parse(std::istream& in);

    //! Writes the program header (e.g., incremental tag) as facts.
    void initProgram(bool incremental) override;
    //! Prepares for a new program step (currently does nothing).
    void beginStep() override;
    //! Writes a rule as facts
    void rule(HeadType ht, AtomSpan head, LitSpan body) override;
    //! Writes a rule as facts
    void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) override;
    //! Writes a minimize directive as facts.
    void minimize(Weight_t prio, WeightLitSpan lits) override;
    //! Writes a projection directive as facts.
    void project(AtomSpan atoms) override;
    //! Writes an output atom directive as facts.
    void outputAtom(Atom_t atom, std::string_view name) override;
    //! Writes an output term directive as facts.
    void outputTerm(Id_t termId, std::string_view name) override;
    //! Writes an output condition as facts.
    void output(Id_t termId, LitSpan condition) override;
    //! Writes an external directive as facts.
    void external(Atom_t a, TruthValue v) override;
    //! Writes assumption directive as facts.
    void assume(LitSpan lits) override;
    //! Writes a heuristic directive as facts.
    void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) override;
    //! Writes an edge directive as facts.
    void acycEdge(int s, int t, LitSpan condition) override;
    //! Writes a theory number term as facts.
    void theoryTerm(Id_t termId, int number) override;
    //! Writes a theory symbolic term as facts.
    void theoryTerm(Id_t termId, std::string_view name) override;
    //! Writes a theory compound term as facts.
    void theoryTerm(Id_t termId, int cId, IdSpan args) override;
    //! Writes a theory element as facts.
    void theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) override;
    //! Writes a theory atom as facts.
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) override;
    //! Writes a theory atom with guard as facts.
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) override;

    //! At the end of each step, writes SCC facts and clears stepData_.
    void endStep() override;

private:
    //! Stores step-specific tuples and graph nodes.
    struct StepData;
    using StepDataPtr = std::unique_ptr<StepData>;
    //! Compute SCCs for a given head and body literals.
    template <typename L>
    void calculateSccs(AtomSpan head, std::span<const L> body);
    //! Print a fact to the output stream.
    template <typename... T>
    void printFact(const char* name, const T&... args);
    //! Print a fact for the current step if step reification is enabled.
    template <typename... T>
    void printStepFact(const char* name, const T&... args);
    //! Insert a tuple into a map and print a fact if it was not already present.
    template <typename M, typename T>
    auto tuple(M& map, const char* name, std::span<T> args) -> size_t;
    //! Insert a theory tuple and return its ID.
    auto theoryTuple(IdSpan args) -> size_t;
    //! Insert a literal tuple and return its ID.
    auto litTuple(LitSpan args) -> size_t;
    //! Insert an atom tuple and return its ID.
    auto atomTuple(AtomSpan args) -> size_t;
    //! Insert a theory element tuple and return its ID.
    auto theoryElementTuple(IdSpan args) -> size_t;
    //! Insert a weighted literal tuple and return its ID.
    auto weightLitTuple(WeightLitSpan args) -> size_t;
    //! Add a node for the given atom to the graph.
    auto addNode(Atom_t atom) -> uint32_t;

    std::ostream& out_;
    StepDataPtr   stepData_;
    size_t        step_ = 0;
    bool          calculateSccs_;
    bool          reifyStep_;
};

} // namespace Potassco
