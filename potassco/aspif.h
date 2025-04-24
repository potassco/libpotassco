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
//! Supported aspif statements.
enum class AspifType : unsigned {
    end       = 0,
    rule      = 1,
    minimize  = 2,
    project   = 3,
    output    = 4,
    external  = 5,
    assume    = 6,
    heuristic = 7,
    edge      = 8,
    theory    = 9,
    comment   = 10
};
POTASSCO_SET_DEFAULT_ENUM_MAX(AspifType::comment);
//! Version 2 output types
enum class OutputType : unsigned { atom = 0, term = 1, cond = 2 };
POTASSCO_SET_DEFAULT_ENUM_MAX(OutputType::cond);
/*!
 * \addtogroup ParseType
 */
///@{
/*!
 * Parses the given program in asp intermediate format and calls `out` on each parsed element.
 */
int readAspif(std::istream& prg, AbstractProgram& out);

//! Supported aspif theory statements.
enum class TheoryType : uint32_t { number = 0, symbol = 1, compound = 2, element = 4, atom = 5, atom_with_guard = 6 };
POTASSCO_SET_DEFAULT_ENUM_MAX(TheoryType::atom_with_guard);

//! Class for parsing logic programs in asp intermediate format.
class AspifInput final : public ProgramReader {
public:
    //! Creates a new parser object that calls `out` on each parsed element.
    explicit AspifInput(AbstractProgram& out, bool mapTerms);

private:
    struct Extra;
    //! Checks whether stream starts with aspif header.
    bool doAttach(bool& inc) override;
    //! Parses the current step and throws an exception on error.
    /*!
     * The function calls beginStep()/endStep() on the associated
     * output object before/after parsing the current step.
     */
    bool doParse() override;
    void matchAtoms();
    void matchLits();
    void matchWLits(bool positive);
    void matchString();
    void matchIds();
    void matchTheory(TheoryType t);
    void matchOutput(OutputType t);
    void outTerm(std::string_view term, LitSpan cond);

    AbstractProgram& out_;
    Extra*           data_;
    uint32_t         version_{0};
    Id_t             nextTerm_{0};
    Atom_t           lastFact_{0};
    bool             mapTerms_{true};
};
///@}

//! Writes a program in potassco's asp intermediate format to the given output stream.
/*!
 * \ingroup WriteType
 */
class AspifOutput : public AbstractProgram {
public:
    //! Creates a new object and associates it with the given output stream.
    /*!
     * \param os Output stream to which program is written.
     * \param version The aspif version to write (or 0 to write the latest version).
     */
    explicit AspifOutput(std::ostream& os, uint32_t version = 0);
    ~AspifOutput() override;
    AspifOutput(const AspifOutput&)            = delete;
    AspifOutput& operator=(const AspifOutput&) = delete;

    //! Writes an aspif header to the stream.
    void initProgram(bool incremental) override;
    //! Prepares the object for a new program step.
    void beginStep() override;
    //! Writes an aspif rule directive.
    void rule(HeadType ht, AtomSpan head, LitSpan body) override;
    //! Writes an aspif rule directive.
    void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan lits) override;
    //! Writes an aspif minimize directive.
    void minimize(Weight_t prio, WeightLitSpan lits) override;
    //! Writes an aspif output (atom) directive.
    /*!
     * \note In version 1, `outputAtom()` is mapped to an output directive with the given atom used as the condition.
     */
    void outputAtom(Atom_t atom, std::string_view name) override;
    //! Writes an output (term) directive.
    /*!
     * \note In version 1, output terms are mapped to output directives over auxiliary atoms.
     */
    void outputTerm(Id_t termId, std::string_view name) override;
    //! Writes an output (cond) directive.
    /*!
     * \note In version 1, output conditions are mapped to output directives over auxiliary term atoms.
     */
    void output(Id_t, LitSpan cond) override;
    //! Writes an aspif external directive.
    void external(Atom_t a, TruthValue v) override;
    //! Writes an aspif assumption directive.
    void assume(LitSpan lits) override;
    //! Writes an aspif projection directive.
    void project(AtomSpan atoms) override;
    //! Writes an aspif edge directive.
    void acycEdge(int s, int t, LitSpan condition) override;
    //! Writes an aspif heuristic directive.
    void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) override;

    //! Writes an aspif theory number term.
    void theoryTerm(Id_t termId, int number) override;
    //! Writes an aspif theory symbolic term.
    void theoryTerm(Id_t termId, std::string_view name) override;
    //! Writes an aspif theory compound term.
    void theoryTerm(Id_t termId, int compound, IdSpan args) override;
    //! Writes an aspif theory element directive.
    void theoryElement(Id_t elementId, IdSpan terms, LitSpan cond) override;
    //! Writes an aspif theory atom directive.
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements) override;
    //! Writes an aspif theory atom directive with guard.
    void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs) override;
    //! Writes the aspif step terminator.
    void endStep() override;

    [[nodiscard]] auto version() const -> unsigned;

private:
    struct Data;
    using DataPtr = std::unique_ptr<Data>;
    //! Starts writing an aspif directive.
    AspifOutput& startDir(AspifType r);
    //! Writes `x`.
    template <typename T>
    AspifOutput& add(T x);
    //! Writes `size(lits)` followed by the elements in `lits`.
    template <typename T>
    AspifOutput& add(std::span<T> lits);
    //! Writes `size(str)` followed by the characters in `str`.
    AspifOutput& add(std::string_view str);
    //! Terminates the active directive by writing a newline.
    AspifOutput& endDir();
    void         auxRule(Atom_t head, LitSpan body);
    //! Maps input to output literals.
    template <typename T>
    auto map(std::span<T>& lits) -> std::span<T>;
    //! Maps input to output atom.
    auto map(Atom_t atom) -> Atom_t;
    //! Creates a new output atom.
    auto newAtom() -> Atom_t { return nextAtom_++; }

    std::ostream& os_;
    DataPtr       data_;
    uint32_t      version_{0};
    uint32_t      identityMax_{0};
    uint32_t      nextAtom_{0};
};
} // namespace Potassco
