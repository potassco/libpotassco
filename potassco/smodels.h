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

#include <functional>
#include <memory>

namespace Potassco {
/*!
 * \addtogroup ParseType
 */
///@{
//! Smodels rule types.
enum class SmodelsRule_t : unsigned {
    End             = 0,  //!< Not a rule, marks the end of all rules.
    Basic           = 1,  //!< Normal rule, i.e. h :- l1, ..., ln.
    Cardinality     = 2,  //!< Cardinality constraint, i.e. h :- lb {l1, ..., ln}.
    Choice          = 3,  //!< Choice rule, i.e. {h1, ... hn} :- l1, ..., ln.
    Generate        = 4,  //!< Generate rule - not supported.
    Weight          = 5,  //!< Weight constraint, i.e. h :- lb {l1=w1, ..., ln=wn}.
    Optimize        = 6,  //!< Optimize rule, i.e. minimize {l1=w1, ..., ln=wn}.
    Disjunctive     = 8,  //!< Normal rule, i.e. h1 | ... | hn :- l1, ..., ln.
    ClaspIncrement  = 90, //!< clasp extension for defining incremental programs.
    ClaspAssignExt  = 91, //!< clasp extension for assigning/declaring external atoms.
    ClaspReleaseExt = 92  //!< clasp extension for releasing external atoms.
};

//! Class for parsing logic programs in (extended) smodels format.
class SmodelsInput : public ProgramReader {
public:
    //! (Optional) lookup function for finding atoms by name. Should return 0 if given name is not a known atom.
    using AtomLookup = std::function<Atom_t(std::string_view name)>;
    //! Options for configuring reading of smodels format.
    struct Options {
        Options() : claspExt(false), cEdge(false), cHeuristic(false), filter(false) {}
        //! Enable clasp extensions for handling incremental programs.
        Options& enableClaspExt() {
            claspExt = true;
            return *this;
        }
        //! Convert _edge/_acyc_ atoms to edge directives.
        Options& convertEdges() {
            cEdge = true;
            return *this;
        }
        //! Convert _heuristic atoms to heuristic directives.
        Options& convertHeuristic() {
            cHeuristic = true;
            return *this;
        }
        //! Remove converted atoms from output.
        Options& dropConverted() {
            filter = true;
            return *this;
        }
        bool claspExt;
        bool cEdge;
        bool cHeuristic;
        bool filter;
    };
    //! Creates a new parser object that calls @c out on each parsed element.
    /*!
     * \note The (optional) lookup function is used to lookup atoms referenced in @a _heuristic predicates.
     *       If not given, SmodelsInput maintains an internal lookup table for this.
     */
    SmodelsInput(AbstractProgram& out, const Options& opts, AtomLookup lookup = nullptr);
    ~SmodelsInput() override;

protected:
    //! Checks whether stream starts with a valid smodels token.
    bool doAttach(bool& inc) override;
    //! Parses the current step and throws exception on error.
    /*!
     * The function calls beginStep()/endStep() on the associated
     * output object before/after parsing the current step.
     */
    bool doParse() override;
    //! Resets internal parsing state.
    void doReset() override;
    //! Reads the current rule block.
    virtual bool readRules();
    //! Reads the current smodels symbol table block.
    virtual bool readSymbols();
    //! Reads the current compute statement.
    virtual bool readCompute();
    //! Reads an optional external block and the number of models.
    virtual bool readExtra();

private:
    struct StringTab;
    void matchBody(RuleBuilder& rule);
    void matchSum(RuleBuilder& rule, bool weights);

    AbstractProgram&           out_;
    AtomLookup                 lookup_;
    std::unique_ptr<StringTab> atoms_;
    std::unique_ptr<StringTab> nodes_;
    Options                    opts_;
};
//! Tries to extract a heuristic modification from a given _heuristic/3 or _heuristic/4 predicate.
bool matchDomHeuPred(std::string_view pred, std::string_view& atom, Heuristic_t& type, int& bias, unsigned& prio);
//! Tries to extract source and target from a given _edge/2 or _acyc_/0 predicate.
bool matchEdgePred(std::string_view pred, std::string_view& n0, std::string_view& n1);

/*!
 * Parses the given program in smodels format and calls out on each parsed element.
 */
int readSmodels(std::istream& prg, AbstractProgram& out, const SmodelsInput::Options& opts = SmodelsInput::Options());

///@}

/*!
 * \addtogroup WriteType
 */
///@{
//! Writes a program in smodels numeric format to the given output stream.
/*!
 * \note The class only supports program constructs that can be directly
 * expressed in smodels numeric format.
 */
class SmodelsOutput : public AbstractProgram {
public:
    //! Creates a new object and associates it with the given output stream.
    /*!
     * If enableClaspExt is true, rules with numbers 90, 91, and 92
     * are used to enable incremental programs and external atoms.
     *
     * The falseAtom is used to write integrity constraints and can be
     * set to 0 if integrity constraints are not used.
     */
    SmodelsOutput(std::ostream& os, bool enableClaspExt, Atom_t falseAtom);
    SmodelsOutput(const SmodelsOutput&)            = delete;
    SmodelsOutput& operator=(const SmodelsOutput&) = delete;

    //! Prepares the object for a new program.
    /*!
     * Requires enableClaspExt or inc must be false.
     */
    void initProgram(bool inc) override;
    //! Starts a new step.
    void beginStep() override;
    //! Writes a basic, choice, or disjunctive rule or throws an exception if rule is not representable.
    void rule(Head_t t, const AtomSpan& head, const LitSpan& body) override;
    //! Writes a cardinality or weight rule or throws an exception if rule is not representable.
    void rule(Head_t t, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) override;
    //! Writes the given minimize rule while ignoring its priority.
    void minimize(Weight_t prio, const WeightLitSpan& lits) override;
    //! Writes the entry (a, str) to the symbol table provided that condition equals a.
    /*!
     * \note Symbols shall only be added once after all rules were added.
     */
    void output(const std::string_view& str, const LitSpan& cond) override;
    //! Writes @c lits as a compute statement.
    /*!
     * \note The function shall be called at most once per step and only after all rules and symbols were added.
     */
    void assume(const LitSpan& lits) override;
    //! Requires enableClaspExt or throws exception.
    void external(Atom_t a, Value_t v) override;
    //! Terminates the current step.
    void endStep() override;

protected:
    //! Starts writing an smodels-rule of type @c rt.
    SmodelsOutput& startRule(SmodelsRule_t rt);
    //! Writes the given head.
    SmodelsOutput& add(Head_t ht, const AtomSpan& head);
    //! Writes the given normal body in smodels format, i.e. @c size(lits) @c size(B-) atoms in B- atoms in B+
    SmodelsOutput& add(const LitSpan& lits);
    //! Writes the given extended body in smodels format.
    SmodelsOutput& add(Weight_t bound, const WeightLitSpan& lits, bool card);
    //! Writes @c i.
    SmodelsOutput& add(unsigned i);
    //! Terminates the active rule by writing a newline.
    SmodelsOutput& endRule();
    //! Returns whether the current program is incremental.
    [[nodiscard]] bool incremental() const { return inc_; }
    //! Returns whether clasp extensions are enabled.
    [[nodiscard]] bool extended() const { return ext_; }

private:
    std::ostream& os_;
    Atom_t        false_;
    int           sec_;
    bool          ext_;
    bool          inc_;
    bool          fHead_;
};
///@}

} // namespace Potassco
