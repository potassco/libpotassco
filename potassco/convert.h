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
#pragma once

#include <potassco/match_basic_types.h>

#include <memory>

namespace Potassco {

//! Converts a given program so that it can be expressed in smodels format.
/*!
 * \ingroup WriteType
 */
class SmodelsConvert : public AbstractProgram {
public:
    //! Creates a new object that passes converted programs to @c out.
    /*!
     * The parameter enableClaspExt determines how heuristic, edge, and external directives are handled.
     * If true, heuristic and edge directives are converted to @a _heuristic and @a _edge predicates, while external
     * directives are passed to @c out.
     * Otherwise, heuristic and edge directives are not converted but directly passed to @c out, while external
     * directives are mapped to choice rules or integrity constraints.
     */
    SmodelsConvert(AbstractProgram& out, bool enableClaspExt);
    ~SmodelsConvert() override;
    SmodelsConvert(SmodelsConvert&&) = delete;

    //! Calls @c initProgram() on the associated output program.
    void initProgram(bool incremental) override;
    //! Calls @c beginStep() on the associated output program.
    void beginStep() override;
    //! Converts the given rule into one or more smodels rules.
    void rule(Head_t t, const AtomSpan& head, const LitSpan& body) override;
    //! Converts the given rule into one or more smodels rules or throws an exception if body contains negative weights.
    void rule(Head_t t, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) override;
    //! Converts literals associated with a priority to a set of corresponding smodels minimize rules.
    void minimize(Weight_t prio, const WeightLitSpan& lits) override;
    //! Adds an atom with the given name that is equivalent to the condition to the symbol table.
    void output(const std::string_view& name, const LitSpan& cond) override;
    //! Marks the atom that is equivalent to @c 'a' as external.
    void external(Atom_t a, Value_t v) override;
    //! Adds an @a _heuristic predicate over the given atom to the symbol table that is equivalent to @c condition.
    void heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition) override;
    //! Adds an @a _edge(s,t) predicate to the symbol table that is equivalent to @c condition.
    void acycEdge(int s, int t, const LitSpan& condition) override;

    //! Finalizes conversion and calls @c endStep() on the associated output program.
    void endStep() override;

    //! Returns the output literal associated to @c in.
    [[nodiscard]] Lit_t get(Lit_t in) const;
    //! Returns the max used smodels atom (valid atoms are [1..n]).
    [[nodiscard]] unsigned maxAtom() const;

protected:
    //! Creates a (named) atom that is equivalent to the given condition.
    Atom_t makeAtom(const LitSpan& lits, bool named);
    //! Processes all outstanding conversions.
    void flush();
    //! Converts external atoms.
    void flushExternal();
    //! Converts minimize statements.
    void flushMinimize();
    //! Converts heuristic directives to _heuristic predicates.
    void flushHeuristic();
    //! Converts (atom,name) pairs to output directives.
    void flushSymbols();

private:
    struct SmData;
    AbstractProgram&        out_;
    std::unique_ptr<SmData> data_;
    bool                    ext_;
};

} // namespace Potassco
