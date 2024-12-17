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

#include <potassco/basic_types.h>
#include <potassco/error.h>

namespace Potassco {

class TheoryData;

/*!
 * \addtogroup BasicTypes
 */
///@{
//! Supported aspif theory terms.
enum class TheoryTermType : uint32_t { number = 0, symbol = 1, compound = 2 };
POTASSCO_SET_DEFAULT_ENUM_MAX(TheoryTermType::compound);

//! Supported aspif theory tuple types.
enum class TupleType { bracket = -3, brace = -2, paren = -1 };
POTASSCO_SET_DEFAULT_ENUM_COUNT(TupleType, 3u, -3);
[[nodiscard]] constexpr auto parens(TupleType t) -> std::string_view {
    using namespace std::literals;
    switch (t) {
        case TupleType::bracket: return "[]"sv;
        case TupleType::brace  : return "{}"sv;
        case TupleType::paren  : return "()"sv;
    }
    POTASSCO_ASSERT_NOT_REACHED("unexpected tuple type");
}

//! A term is either a number, symbolic, or compound term (function or tuple).
class TheoryTerm {
public:
    TheoryTerm() noexcept = default;
    //! Term type.
    using Type = TheoryTermType;
    //! Iterator type for iterating over arguments of a compound term.
    using iterator = const Id_t*; // NOLINT
    //! Returns the type of this term.
    [[nodiscard]] Type type() const;
    //! Returns the number stored in this or throws if type() != Number.
    [[nodiscard]] int number() const;
    //! Returns the symbol stored in this or throws if type() != Symbol.
    [[nodiscard]] const char* symbol() const;
    //! Returns the compound id (either term id or tuple type) stored in this or throws if type() != Compound.
    [[nodiscard]] int compound() const;
    //! Returns whether this is a function.
    [[nodiscard]] bool isFunction() const;
    //! Returns the function id stored in this or throws if not isFunction().
    [[nodiscard]] Id_t function() const;
    //! Returns whether this is a tuple.
    [[nodiscard]] bool isTuple() const;
    //! Returns the tuple id stored in this or throws if not isTuple().
    [[nodiscard]] TupleType tuple() const;
    //! Returns the number of arguments in this term.
    [[nodiscard]] uint32_t size() const;
    //! Returns an iterator pointing to the first argument of this term.
    [[nodiscard]] iterator begin() const;
    //! Returns an iterator marking the end of the arguments of this term.
    [[nodiscard]] iterator end() const;
    //! Returns the range [begin(), end()).
    [[nodiscard]] IdSpan terms() const { return {begin(), size()}; }

private:
    TheoryTerm(uint64_t d) noexcept : data_(d) {}
    struct FuncData;
    friend class TheoryData;
    [[nodiscard]] uintptr_t getPtr() const;
    [[nodiscard]] FuncData* func() const;

    uint64_t data_ = 0;
};

//! A basic building block for a theory atom.
class TheoryElement {
public:
    TheoryElement(const TheoryElement&)            = delete;
    TheoryElement& operator=(const TheoryElement&) = delete;

    //! Iterator type for iterating over the terms of an element.
    using iterator = const Id_t*; // NOLINT
    //! Returns the number of terms belonging to this element.
    [[nodiscard]] uint32_t size() const { return nTerms_; }
    //! Returns an iterator pointing to the first term of this element.
    [[nodiscard]] iterator begin() const { return term_; }
    //! Returns an iterator one past the last term of this element.
    [[nodiscard]] iterator end() const { return begin() + size(); }
    //! Returns the terms of this element.
    [[nodiscard]] IdSpan terms() const { return {begin(), size()}; }
    //! Returns the condition associated with this element.
    [[nodiscard]] Id_t condition() const;

private:
    friend class TheoryData;
    TheoryElement(const IdSpan& terms, const Id_t* c);
    void     setCondition(Id_t c);
    uint32_t nTerms_ : 31;
    uint32_t nCond_  : 1;
    POTASSCO_WARNING_BEGIN_RELAXED
    Id_t term_[0];
    POTASSCO_WARNING_END_RELAXED
};

//! A theory atom.
class TheoryAtom {
public:
    TheoryAtom(const TheoryAtom&)            = delete;
    TheoryAtom& operator=(const TheoryAtom&) = delete;
    //! Iterator type for iterating over the elements of a theory atom.
    using iterator = const Id_t*; // NOLINT
    //! Returns the associated program atom or 0 if this originated from a directive.
    [[nodiscard]] Id_t atom() const { return static_cast<Id_t>(atom_); }
    //! Returns the term that is associated with this atom.
    [[nodiscard]] Id_t term() const { return termId_; }
    //! Returns the number of elements in this atom.
    [[nodiscard]] uint32_t size() const { return nTerms_; }
    //! Returns an iterator pointing to the first element of this atom.
    [[nodiscard]] iterator begin() const { return term_; }
    //! Returns an iterator marking the end of elements of this atom.
    [[nodiscard]] iterator end() const { return begin() + size(); }
    //! Returns the range [begin(), end()).
    [[nodiscard]] IdSpan elements() const { return {begin(), size()}; }
    //! Returns a pointer to the id of the theory operator associated with this atom or 0 if atom has no guard.
    [[nodiscard]] const Id_t* guard() const;
    //! Returns a pointer to the term id of the right hand side of the theory operator or 0 if atom has no guard.
    [[nodiscard]] const Id_t* rhs() const;

private:
    friend class TheoryData;
    TheoryAtom(Id_t atom, Id_t term, const IdSpan& elements, const Id_t* op, const Id_t* rhs);
    uint32_t atom_  : 31;
    uint32_t guard_ : 1;
    Id_t     termId_;
    uint32_t nTerms_;
    POTASSCO_WARNING_BEGIN_RELAXED
    Id_t term_[0];
    POTASSCO_WARNING_END_RELAXED
};

//! A type for storing and looking up theory atoms and their elements and terms.
class TheoryData {
public:
    //! Iterator type for iterating over the theory atoms of a TheoryData object.
    using atom_iterator = const TheoryAtom* const*; // NOLINT
    using Term          = TheoryTerm;
    using Element       = TheoryElement;
    TheoryData();
    ~TheoryData();
    TheoryData(TheoryData&&) = delete;

    //! Sentinel for marking a condition to be set later.
    static constexpr auto cond_deferred = static_cast<Id_t>(-1);

    //! Resets this object to the state after default construction.
    void reset();
    //! May be called to distinguish between the current and a previous incremental step.
    void update();

    //! Adds a new theory atom.
    /*!
     * Each element in elements shall be an id associated with an atom element
     * eventually added via addElement().
     */
    void addAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements);
    //! Adds a new theory atom with guard and right hand side.
    void addAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elements, Id_t op, Id_t rhs);

    //! Adds a new theory atom element with the given id.
    /*!
     * Each element in terms shall be an id of a theory term
     * eventually added via one of the addTerm() overloads.
     * \note If cond is @c cond_deferred, the condition may later be changed via a call to @c setCondition().
     */
    void addElement(Id_t elementId, const IdSpan& terms, Id_t cond = cond_deferred);
    //! Changes the condition of the element with the given id.
    /*!
     * \pre The element was previously added with condition @c cond_deferred.
     */
    void setCondition(Id_t elementId, Id_t newCond);

    //! Adds a new number term with the given id.
    void addTerm(Id_t termId, int number);
    //! Adds a new symbolic term with the given name and id.
    void addTerm(Id_t termId, const std::string_view& name);
    //! Adds a new symbolic term with the given name and id.
    void addTerm(Id_t termId, const char* name);
    //! Adds a new function term with the given id.
    /*!
     * The parameter funcSym represents the name of the function and shall be the id of a symbolic term.
     * Each element in args shall be an id of a theory term.
     */
    void addTerm(Id_t termId, Id_t funcSym, const IdSpan& args);
    //! Adds a new tuple term with the given id.
    void addTerm(Id_t termId, TupleType type, const IdSpan& args);

    //! Removes the term with the given id.
    /*!
     * \note It is the caller's responsibility to ensure that the removed term is not referenced
     * by any theory element.
     * \note The term id of a removed term may be reused in a subsequent call to @c addTerm().
     */
    void removeTerm(Id_t termId);

    //! Returns the number of stored theory atoms.
    [[nodiscard]] uint32_t numAtoms() const;
    //! Returns an iterator pointing to the first theory atom.
    [[nodiscard]] atom_iterator begin() const;
    //! Returns an iterator pointing to the first theory atom added after last call to update.
    [[nodiscard]] atom_iterator currBegin() const;
    //! Returns an iterator marking the end of the range of theory atoms.
    [[nodiscard]] atom_iterator end() const;
    //! Returns whether this object stores a term with the given id.
    [[nodiscard]] bool hasTerm(Id_t id) const;
    //! Returns whether the given term was added after last call to update.
    [[nodiscard]] bool isNewTerm(Id_t id) const;
    //! Returns whether this object stores an atom element with the given id.
    [[nodiscard]] bool hasElement(Id_t id) const;
    //! Returns whether the given element was added after last call to update.
    [[nodiscard]] bool isNewElement(Id_t id) const;
    //! Returns the term with the given id or throws if no such term exists.
    [[nodiscard]] Term getTerm(Id_t id) const;
    //! Returns the element with the given id or throws if no such element exists.
    [[nodiscard]] const Element& getElement(Id_t id) const;

    //! Removes all theory atoms @c a for which @c f(a) returns true.
    template <class F>
    void filter(const F& f) {
        auto** j   = const_cast<TheoryAtom**>(currBegin());
        auto   pop = 0u;
        for (atom_iterator it = j, end = this->end(); it != end; ++it) {
            if (auto atom = (*it)->atom(); not atom || not f(**it)) {
                *j++ = const_cast<TheoryAtom*>(*it);
            }
            else {
                ++pop;
                destroyAtom(const_cast<TheoryAtom*>(*it));
            }
        }
        resizeAtoms(numAtoms() - pop);
    }
    //! Interface for visiting a theory.
    class Visitor {
    public:
        virtual ~Visitor() = default;
        //! Visit a theory term. Should call <tt>data.accept(t, *this)</tt> to visit any arguments of the term.
        virtual void visit(const TheoryData& data, Id_t termId, const TheoryTerm& t) = 0;
        //! Visit a theory element. Should call <tt>data.accept(e, *this)</tt> to visit the terms of the element.
        virtual void visit(const TheoryData& data, Id_t elemId, const TheoryElement& e) = 0;
        //! Visit the theory atom. Should call <tt>data.accept(atom, *this)</tt> to visit the elements of the atom.
        virtual void visit(const TheoryData& data, const TheoryAtom& atom) = 0;
    };
    //! Possible visitation modes.
    /*!
     * Mode @c visit_current ignores atoms, elements, or terms
     * that were added in previous steps, i.e. before the last call to update().
     */
    enum VisitMode { visit_all, visit_current };
    //! Calls <tt>out.visit(*this, a)</tt> for all theory atoms.
    void accept(Visitor& out, VisitMode m = visit_current) const;
    //! Visits terms and elements of the given atom.
    void accept(const TheoryAtom& a, Visitor& out, VisitMode m = visit_all) const;
    //! Visits terms of the given element.
    void accept(const TheoryElement& e, Visitor& out, VisitMode m = visit_all) const;
    //! If given term is a compound term, visits its sub terms.
    void accept(const TheoryTerm& t, Visitor& out, VisitMode m = visit_all) const;

private:
    struct DestroyT;
    [[nodiscard]] uint32_t numTerms() const;
    [[nodiscard]] uint32_t numElems() const;

    TheoryTerm& setTerm(Id_t);
    void        resizeAtoms(uint32_t n);
    static void destroyAtom(TheoryAtom*);
    // NOLINTBEGIN(modernize-use-nodiscard)
    bool doVisitTerm(VisitMode m, Id_t id) const { return m == visit_all || isNewTerm(id); }
    bool doVisitElem(VisitMode m, Id_t id) const { return m == visit_all || isNewElement(id); }
    // NOLINTEND(modernize-use-nodiscard)
    struct Data;
    std::unique_ptr<Data> data_;
};

inline void print(AbstractProgram& out, Id_t termId, const TheoryTerm& term) {
    switch (term.type()) {
        case TheoryTermType::number  : out.theoryTerm(termId, term.number()); return;
        case TheoryTermType::symbol  : out.theoryTerm(termId, term.symbol()); return;
        case TheoryTermType::compound: out.theoryTerm(termId, term.compound(), term.terms()); return;
    }
    POTASSCO_ASSERT_NOT_REACHED("invalid term");
}
inline void print(AbstractProgram& out, const TheoryAtom& a) {
    if (a.guard()) {
        out.theoryAtom(a.atom(), a.term(), a.elements(), *a.guard(), *a.rhs());
    }
    else {
        out.theoryAtom(a.atom(), a.term(), a.elements());
    }
}
///@}
} // namespace Potassco
