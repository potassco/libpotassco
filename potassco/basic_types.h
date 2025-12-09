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

/*!
 * \mainpage notitle
 * A small library for parsing and converting logic programs in aspif and smodels format.
 *
 * The library contains parsers and writers for aspif and smodels format
 * as well as functions and types for converting between the two formats
 * to the extent possible.
 *
 * A specification of aspif can be found in Appendix A of:
 * https://www.cs.uni-potsdam.de/wv/publications/DBLP_conf/iclp/GebserKKOSW16x.pdf
 */
#include <potassco/platform.h>

#include <potassco/bits.h>
#include <potassco/enum.h>

#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#define LIB_POTASSCO_VERSION_MAJOR 2
#define LIB_POTASSCO_VERSION_MINOR 0
#define LIB_POTASSCO_VERSION_PATCH 0
#define LIB_POTASSCO_VERSION                                                                                           \
    POTASSCO_STRING(LIB_POTASSCO_VERSION_MAJOR)                                                                        \
    "." POTASSCO_STRING(LIB_POTASSCO_VERSION_MINOR) "." POTASSCO_STRING(LIB_POTASSCO_VERSION_PATCH)

//! Root namespace for all types and functions of libpotassco.
namespace Potassco {

/*!
 * \defgroup WriteType Program writer types
 * \brief Types and functions for writing logic programs.
 */

/*!
 * \defgroup ParseType Program parser types
 * \brief Types and functions for parsing logic programs.
 */

/*!
 * \defgroup BasicTypes Basic Data Types
 * \brief Basic types for working with logic programs.
 */
///@{
//! Ids are non-negative integers in the range [0;id_max].
using Id_t = uint32_t;
//! Maximum value for ids.
constexpr auto id_max = static_cast<Id_t>(-1);
//! Atom ids are positive integers in the range [atom_min;atom_max].
using Atom_t = uint32_t;
//! Minimum value for atom ids (must not be 0).
constexpr auto atom_min = static_cast<Atom_t>(1u);
//! Maximum value for atom ids.
constexpr auto atom_max = static_cast<Atom_t>(((1u) << 31) - 1);
//! Literals are signed atoms.
using Lit_t = int32_t;
//! (Literal) weights are integers.
using Weight_t = int32_t;
//! A literal with an associated weight.
struct WeightLit {
    Lit_t    lit;    //!< Literal.
    Weight_t weight; //!< Associated weight.

    friend constexpr bool operator==(const WeightLit& lhs, const WeightLit& rhs) noexcept  = default;
    friend constexpr auto operator<=>(const WeightLit& lhs, const WeightLit& rhs) noexcept = default;
    friend constexpr auto operator==(const WeightLit& lhs, Lit_t rhs) noexcept {
        return lhs.lit == rhs && lhs.weight == 1;
    }
    friend constexpr auto operator<=>(const WeightLit& lhs, Lit_t rhs) noexcept {
        return lhs <=> WeightLit{.lit = rhs, .weight = 1};
    }
};

using IdSpan        = std::span<const Id_t>;
using AtomSpan      = std::span<const Atom_t>;
using LitSpan       = std::span<const Lit_t>;
using WeightLitSpan = std::span<const WeightLit>;
//! Convert a single lvalue into a span with one element.
template <typename T>
requires(std::is_lvalue_reference_v<T>)
constexpr auto toSpan(T&& x) -> std::span<std::remove_reference_t<T>> {
    return {&x, 1};
}

//! Supported rule head types.
enum class HeadType : unsigned { disjunctive = 0, choice = 1 };
POTASSCO_SET_DEFAULT_ENUM_MAX(HeadType::choice);

//! Supported rule body types.
enum class BodyType : unsigned { normal = 0, sum = 1, count = 2 };
POTASSCO_SET_DEFAULT_ENUM_MAX(BodyType::count);

//! Type representing a truth or external value.
enum class TruthValue : unsigned { free = 0, true_ = 1, false_ = 2, release = 3 };
POTASSCO_SET_ENUM_ENTRIES(TruthValue, {free, "free"sv}, {true_, "true"sv}, {false_, "false"sv}, {release, "release"sv});

//! Supported modifications for domain heuristic.
enum class DomModifier : unsigned { level = 0, sign = 1, factor = 2, init = 3, true_ = 4, false_ = 5 };
POTASSCO_SET_ENUM_ENTRIES(DomModifier, {level, "level"sv}, {sign, "sign"sv}, {factor, "factor"sv}, {init, "init"sv},
                          {true_, "true"sv}, {false_, "false"sv});
POTASSCO_ENABLE_CMP_OPS(DomModifier);

//! Basic callback interface for constructing a logic program.
class AbstractProgram {
public:
    virtual ~AbstractProgram();
    //! Called once to prepare for a new logic program.
    virtual void initProgram(bool incremental);
    //! Called once before rules and directives of the current program step are added.
    virtual void beginStep();

    //! Add the given rule to the program.
    virtual void rule(HeadType ht, AtomSpan head, LitSpan body) = 0;
    //! Add the given sum rule to the program.
    virtual void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) = 0;
    //! Add the given minimize statement to the program.
    virtual void minimize(Weight_t prio, WeightLitSpan lits) = 0;

    //! Output `name` whenever the given atom is true in a stable model.
    virtual void outputAtom(Atom_t atom, std::string_view name) = 0;

    /*!
     * \name Advanced
     * Functions for adding advanced constructs.
     * By default, functions in this group throw a std::logic_error()
     * to signal that advanced constructs are not supported.
     */
    //@{
    //! Add new output term.
    virtual void outputTerm(Id_t termId, std::string_view name);
    //! Output a previously added output term whenever `condition` is true in a stable model.
    /*!
     * \pre outputTerm() was previously called with the given termId.
     * \note The function may be called multiple times for a given term. The term shall be shown whenever at least
     *       one of the provided conditions is true in a stable model.
     */
    virtual void output(Id_t termId, LitSpan condition);
    //! Mark the given list of atoms as projection atoms.
    virtual void project(AtomSpan atoms);
    //! If `v` is not equal to `TruthValue::release`, mark `a` as external and assume value `v`. Otherwise, treat `a` as
    //! a regular atom.
    virtual void external(Atom_t a, TruthValue v);
    //! Assume the given literals to true during solving.
    virtual void assume(LitSpan lits);
    //! Apply the given heuristic modification to atom `a` whenever `condition` is true.
    virtual void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition);
    //! Assume an edge between `s` and `t` whenever `condition` is true.
    virtual void acycEdge(int s, int t, LitSpan condition);
    //@}

    /*!
     * \name Theory data
     * Functions for adding theory statements.
     * By default, all theory functions throw a std::logic_error().
     * Note, ids shall be unique within one step.
     */
    //@{
    //! Add a new number term.
    virtual void theoryTerm(Id_t termId, int number);
    //! Add a new symbolic term.
    virtual void theoryTerm(Id_t termId, std::string_view name);
    //! Add a new compound (function or tuple) term.
    virtual void theoryTerm(Id_t termId, int cId, IdSpan args);
    //! Add a new theory atom element.
    virtual void theoryElement(Id_t elementId, IdSpan terms, LitSpan cond);
    //! Add a new theory atom consisting of the given elements, which have to be added eventually.
    virtual void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements);
    //! Add a new theory atom with guard and right-hand side.
    virtual void theoryAtom(Id_t atomOrZero, Id_t termId, IdSpan elements, Id_t op, Id_t rhs);
    //@}

    //! Called once after all rules and directives of the current program step were added.
    virtual void endStep();
};
class RuleBuilder;

/*!
 * \defgroup BasicFunc Basic functions
 * \brief Additional functions over basic types.
 * \ingroup BasicTypes
 */
///@{
//! Returns whether `n` is a valid atom number (i.e. in the range [atomMin;atomMax])
template <std::integral T>
constexpr bool validAtom(T n) {
    return std::cmp_greater_equal(n, atom_min) && std::cmp_less_equal(n, atom_max);
}
//! Identity function for atoms.
constexpr Atom_t atom(Atom_t atom) { return atom; }
//! Returns the atom of the given literal.
constexpr Atom_t atom(Lit_t lit) { return static_cast<Atom_t>(lit >= 0 ? lit : -lit); }
//! Returns the atom of the given weight literal.
constexpr Atom_t atom(const WeightLit& w) { return atom(w.lit); }
//! Returns the positive literal of the given atom.
constexpr Lit_t lit(Atom_t atom) { return static_cast<Lit_t>(atom); }
//! Identity function for literals.
constexpr Lit_t lit(Lit_t lit) { return lit; }
//! Returns the literal of the given weight literal.
constexpr Lit_t lit(const WeightLit& w) { return w.lit; }
//! Returns the negative literal of the given atom.
constexpr Lit_t neg(Atom_t a) { return -lit(a); }
//! Returns the complement of the given literal.
constexpr Lit_t neg(Lit_t lit) { return -lit; }
//! Returns the weight of the given atom, which is always 1.
constexpr Weight_t weight(Atom_t) { return 1; }
//! Returns the weight of the given literal, which is always 1.
constexpr Weight_t weight(Lit_t) { return 1; }
//! Returns the weight of the given weight literal.
constexpr Weight_t weight(const WeightLit& w) { return w.weight; }

//! Atom comparison flags.
enum class AtomCompare : uint8_t { cmp_default = 0, cmp_natural = 1, cmp_arity = 2 };
POTASSCO_ENABLE_BIT_OPS(AtomCompare);
//! Returns whether lhsAtom is less than rhsAtom according to the compare flags.
auto cmpAtom(std::string_view lhsAtom, std::string_view rhsAtom, AtomCompare cmp) noexcept -> std::strong_ordering;

//! Returns predicate name, arity, and arguments for the given atom.
/*!
 * Given an atom `foo(x,y)`, the function returns the tuple ("foo"sv,2,"x,y"sv).
 * If `atom` is not a valid atom name, the returned arity will be < 0.
 */
auto atomSymbol(std::string_view atom) -> std::tuple<std::string_view, int, std::string_view>;
//! Returns predicate name and arity for the given atom.
inline auto predicate(std::string_view atom) -> std::pair<std::string_view, int> {
    auto [p, a, _] = atomSymbol(atom);
    return std::pair{p, a};
}
enum class AtomArgMode { raw, unquote };
enum class AtomArg { first, last };
POTASSCO_SET_ENUM_ENTRIES(AtomArg, {first, "first"sv}, {last, "last"sv});
//! Removes and returns the first/last atom argument from `args`.
auto popArg(std::string_view& args, AtomArg argPos, AtomArgMode mode) -> std::string_view;
///@}

///@}

} // namespace Potassco
