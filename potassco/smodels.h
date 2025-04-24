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
/*!
 * \addtogroup ParseType
 */
///@{
//! Smodels rule types.
enum class SmodelsType : unsigned {
    end               = 0,  //!< Not a rule, marks the end of all rules.
    basic             = 1,  //!< Normal rule, i.e. `h :- l1, ..., ln`.
    cardinality       = 2,  //!< Cardinality constraint, i.e. `h :- lb {l1, ..., ln}`.
    choice            = 3,  //!< Choice rule, i.e. `{h1, ... hn} :- l1, ..., ln`.
    generate          = 4,  //!< Generate rule - not supported.
    weight            = 5,  //!< Weight constraint, i.e. `h :- lb {l1=w1, ..., ln=wn}`.
    optimize          = 6,  //!< Optimize rule, i.e. `#minimize {l1=w1, ..., ln=wn}`.
    disjunctive       = 8,  //!< Normal rule, i.e. `h1 | ... | hn :- l1, ..., ln`.
    clasp_increment   = 90, //!< clasp extension for defining incremental programs.
    clasp_assign_ext  = 91, //!< clasp extension for assigning/declaring external atoms.
    clasp_release_ext = 92  //!< clasp extension for releasing external atoms.
};

//! Class for parsing logic programs in (extended) smodels format.
class SmodelsInput final : public ProgramReader {
public:
    //! Options for configuring reading of smodels format.
    struct Options {
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
        //! Remove converted atoms from the output.
        Options& dropConverted() {
            filter = true;
            return *this;
        }
        bool claspExt{false};
        bool cEdge{false};
        bool cHeuristic{false};
        bool filter{false};
    };
    //! Creates a new parser object that calls `out` on each parsed element.
    SmodelsInput(AbstractProgram& out, const Options& opts);
    ~SmodelsInput() override;

private:
    struct Extra;
    //! Checks whether the input stream starts with a valid smodels token.
    bool doAttach(bool& inc) override;
    //! Parses the current step and throws an exception on error.
    /*!
     * The function calls beginStep()/endStep() on the associated
     * output object before/after parsing the current step.
     */
    bool doParse() override;
    //! Resets internal parsing state.
    void doReset() override;
    //! Reads the current rule block.
    void readRules();
    //! Reads the current smodels symbol table block.
    void readSymbols();
    //! Reads the current compute statement.
    void readCompute();
    //! Reads an optional external block and the number of models.
    void readExtra();
    void matchBody(RuleBuilder& rule);
    void matchSum(RuleBuilder& rule, bool weights);
    bool mapSymbol(Atom_t atom, std::string_view name);

    AbstractProgram&       out_;
    std::unique_ptr<Extra> extra_;
    Options                opts_;
};
//! Tries to extract a heuristic modification from a given _heuristic/3 or _heuristic/4 predicate.
bool matchDomHeuPred(std::string_view pred, std::string_view& atom, DomModifier& type, int& bias, unsigned& prio);
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
//! Writes a program in the smodels numeric format to the given output stream.
/*!
 * \note The class only supports program constructs that can be directly
 * expressed in smodels numeric format.
 */
class SmodelsOutput final : public AbstractProgram {
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
    //! Writes a basic, choice, or disjunctive rule or throws an exception if the rule is not representable.
    void rule(HeadType ht, AtomSpan head, LitSpan body) override;
    //! Writes a cardinality or weight rule or throws an exception if the rule is not representable.
    void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) override;
    //! Writes the given minimize rule ignoring its priority.
    void minimize(Weight_t prio, WeightLitSpan lits) override;
    //! Writes the entry (atom, name) to the symbol table.
    /*!
     * \note Symbols shall only be added once after all rules were added.
     */
    void outputAtom(Atom_t atom, std::string_view name) override;
    //! Writes `lits` as a compute statement.
    /*!
     * \note The function shall be called at most once per step, and only after all rules and symbols were added.
     */
    void assume(LitSpan lits) override;
    //! Requires enableClaspExt or throws exception.
    void external(Atom_t a, TruthValue v) override;
    //! Terminates the current step.
    void endStep() override;

private:
    //! Starts writing a rule of type `rt`.
    SmodelsOutput& startRule(SmodelsType rt);
    //! Writes the given head.
    SmodelsOutput& add(HeadType ht, AtomSpan head);
    //! Writes the given normal body in smodels format, i.e. `size(lits)` `size(B-)` `atoms in B-` `atoms in B+`
    SmodelsOutput& add(LitSpan lits);
    //! Writes the given extended body in smodels format.
    SmodelsOutput& add(Weight_t bound, WeightLitSpan lits, bool card);
    //! Writes `i`.
    SmodelsOutput& add(unsigned i);
    //! Terminates the active rule by writing a newline.
    SmodelsOutput& endRule();

    std::ostream& os_;
    Atom_t        false_;
    int           sec_;
    bool          ext_;
    bool          inc_;
    bool          fHead_;
};
///@}

} // namespace Potassco
