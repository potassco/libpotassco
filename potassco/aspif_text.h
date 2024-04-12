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
#pragma once
#include <potassco/match_basic_types.h>
#include <potassco/theory_data.h>

#include <cstring>

namespace Potassco {
//! Class for parsing logic programs in ground text format.
/*!
 * \ingroup ParseType
 */
class AspifTextInput : public ProgramReader {
public:
    //! Creates a new object and associates it with the given output if any.
    AspifTextInput(AbstractProgram* out);
    //! Sets the program to which parsed elements should be output.
    void setOutput(AbstractProgram& out);

protected:
    //! Checks whether stream starts with a valid token.
    bool doAttach(bool& inc) override;
    //! Attempts to parses the current step or throws an exception on error.
    /*!
     * The function calls beginStep()/endStep() on the associated
     * output object before/after parsing the current step.
     */
    bool doParse() override;
    //! Parses statements until next step directive or input is exhausted.
    void parseStatements();

private:
    void             skipws();
    bool             matchDirective();
    void             matchRule(char peek);
    void             matchAtoms(const char* seps);
    void             matchLits();
    void             matchCondition();
    void             matchAgg();
    void             matchDelim(char);
    bool             matchOpt(const char* ts);
    Atom_t           matchId();
    Lit_t            matchLit();
    int              matchInt();
    void             matchTerm();
    void             matchAtomArg();
    void             matchStr();
    const char*      matchWord();
    void             push(char c);
    AbstractProgram* out_;
    struct Data;
    Data* data_;
};

//! Class for writing logic programs in ground text format.
/*!
 * Writes a logic program in human-readable text format.
 * \ingroup WriteType
 */
class AspifTextOutput : public Potassco::AbstractProgram {
public:
    AspifTextOutput(std::ostream& os);
    ~AspifTextOutput() override;
    AspifTextOutput(AspifTextOutput&&) = delete;

    void initProgram(bool incremental) override;
    void beginStep() override;
    void rule(Head_t ht, const AtomSpan& head, const LitSpan& body) override;
    void rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) override;
    void minimize(Weight_t prio, const WeightLitSpan& lits) override;
    void output(const std::string_view& str, const LitSpan& cond) override;
    void external(Atom_t a, Value_t v) override;
    void assume(const LitSpan& lits) override;
    void project(const AtomSpan& atoms) override;
    void acycEdge(int s, int t, const LitSpan& condition) override;
    void heuristic(Atom_t a, Heuristic_t t, int bias, unsigned prio, const LitSpan& condition) override;

    void theoryTerm(Id_t termId, int number) override;
    void theoryTerm(Id_t termId, const std::string_view& name) override;
    void theoryTerm(Id_t termId, int compound, const IdSpan& args) override;
    void theoryElement(Id_t elementId, const IdSpan& terms, const LitSpan& cond) override;
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) override;
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) override;
    void endStep() override;

    void addAtom(Atom_t id, const std::string_view& str);

private:
    std::ostream&    printName(std::ostream& os, Lit_t lit) const;
    std::ostream&    printName(std::ostream& os, Atom_t at) const { return printName(os, lit(at)); }
    std::ostream&    printTheoryAtom(std::ostream&, const TheoryAtom&) const;
    std::ostream&    appendTerm(std::ostream&, Id_t term) const;
    void             writeDirectives();
    void             visitTheoryAtoms();
    AspifTextOutput& push(uint32_t x);
    AspifTextOutput& push(const AtomSpan& atoms);
    AspifTextOutput& push(const LitSpan& lits);
    AspifTextOutput& push(const WeightLitSpan& wlits);
    template <typename EnumT>
    AspifTextOutput& push(EnumT t) {
        return push(static_cast<uint32_t>(t));
    }

    std::ostream& os_;
    struct Data;
    TheoryData theory_;
    Data*      data_;
    int        step_;
};

} // namespace Potassco
