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

namespace Potassco {
/*!
 * \addtogroup ParseType
 */
///@{
/*!
 * Parses the given program in asp intermediate format and calls @c out on each parsed element.
 */
int readAspif(std::istream& prg, AbstractProgram& out);

//! Supported aspif theory statements.
enum class TheoryType : uint32_t { number = 0, symbol = 1, compound = 2, element = 4, atom = 5, atom_with_guard = 6 };
POTASSCO_SET_DEFAULT_ENUM_MAX(TheoryType::atom_with_guard);

//! Class for parsing logic programs in asp intermediate format.
class AspifInput : public ProgramReader {
public:
    //! Creates a new parser object that calls @c out on each parsed element.
    explicit AspifInput(AbstractProgram& out);

protected:
    //! Checks whether stream starts with aspif header.
    bool doAttach(bool& inc) override;
    //! Parses the current step and throws an exception on error.
    /*!
     * The function calls beginStep()/endStep() on the associated
     * output object before/after parsing the current step.
     */
    bool doParse() override;

private:
    struct Extra;
    void matchAtoms();
    void matchLits();
    void matchWLits(bool positive);
    void matchString();
    void matchIds();
    void matchTheory(TheoryType t);

    AbstractProgram& out_;
    Extra*           data_;
};
///@}

//! Writes a program in potassco's asp intermediate format to the given output stream.
/*!
 * \ingroup WriteType
 */
class AspifOutput : public AbstractProgram {
public:
    //! Creates a new object and associates it with the given output stream.
    explicit AspifOutput(std::ostream& os);
    AspifOutput(const AspifOutput&)            = delete;
    AspifOutput& operator=(const AspifOutput&) = delete;

    //! Writes an aspif header to the stream.
    void initProgram(bool incremental) override;
    //! Prepares the object for a new program step.
    void beginStep() override;
    //! Writes an aspif rule directive.
    void rule(HeadType ht, const AtomSpan& head, const LitSpan& body) override;
    //! Writes an aspif rule directive.
    void rule(HeadType ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& lits) override;
    //! Writes an aspif minimize directive.
    void minimize(Weight_t prio, const WeightLitSpan& lits) override;
    //! Writes an aspif output directive.
    void output(const std::string_view& str, const LitSpan& cond) override;
    //! Writes an aspif external directive.
    void external(Atom_t a, TruthValue v) override;
    //! Writes an aspif assumption directive.
    void assume(const LitSpan& lits) override;
    //! Writes an aspif projection directive.
    void project(const AtomSpan& atoms) override;
    //! Writes an aspif edge directive.
    void acycEdge(int s, int t, const LitSpan& condition) override;
    //! Writes an aspif heuristic directive.
    void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, const LitSpan& condition) override;

    //! Writes an aspif theory number term.
    void theoryTerm(Id_t termId, int number) override;
    //! Writes an aspif theory symbolic term.
    void theoryTerm(Id_t termId, const std::string_view& name) override;
    //! Writes an aspif theory compound term.
    void theoryTerm(Id_t termId, int compound, const IdSpan& args) override;
    //! Writes an aspif theory element directive.
    void theoryElement(Id_t elementId, const IdSpan& terms, const LitSpan& cond) override;
    //! Writes an aspif theory atom directive.
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements) override;
    //! Writes an aspif theory atom directive with guard.
    void theoryAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs) override;
    //! Writes the aspif step terminator.
    void endStep() override;

protected:
    //! Starts writing an aspif directive.
    AspifOutput& startDir(AspifType r);
    //! Writes @c x.
    template <typename T>
    AspifOutput& add(T x);
    //! Writes @c size(lits) followed by the elements in @c lits.
    template <typename T>
    AspifOutput& add(const std::span<const T>& lits);
    //! Writes @c size(str) followed by the characters in @c str.
    AspifOutput& add(const std::string_view& str);
    //! Terminates the active directive by writing a newline.
    AspifOutput& endDir();

private:
    std::ostream& os_;
};
} // namespace Potassco
