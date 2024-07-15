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
#include <potassco/theory_data.h>

#include <potassco/error.h>

#include <amc/vector.hpp>

#include <algorithm>
#include <cstring>

#include <stdexcept>
namespace Potassco {
template <typename T>
static constexpr std::size_t computeExtraBytes(const T& arg [[maybe_unused]]) {
    if constexpr (requires { arg.size(); }) {
        return arg.size() * sizeof(typename T::value_type);
    }
    else if constexpr (std::is_pointer_v<T>) {
        return (arg != nullptr) * sizeof(std::remove_pointer_t<T>);
    }
    else {
        static_assert(std::is_trivial_v<T>);
        return 0;
    }
}

struct TheoryTerm::FuncData {
    FuncData(int32_t b, const IdSpan& a) : base(b), size(static_cast<uint32_t>(a.size())) {
        std::ranges::copy(a, args);
    }
    int32_t  base;
    uint32_t size;
    POTASSCO_WARNING_BEGIN_RELAXED
    Id_t args[0];
    POTASSCO_WARNING_END_RELAXED
};

constexpr uint64_t c_nulTerm  = static_cast<uint64_t>(-1);
constexpr uint64_t c_typeMask = static_cast<uint64_t>(3);
static uint64_t    assertPtr(const void* p, uint32_t mask) {
    auto data = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    POTASSCO_ASSERT(not test_any(data, mask), "Invalid pointer alignment");
    return data | mask;
}
Theory_t TheoryTerm::type() const { return static_cast<Theory_t>(clear_mask(data_, ~c_typeMask)); }
int      TheoryTerm::number() const {
    POTASSCO_CHECK(type() == Theory_t::Number, Errc::invalid_argument, "Term is not a number");
    return static_cast<int>(data_ >> 2);
}
uintptr_t   TheoryTerm::getPtr() const { return static_cast<uintptr_t>(clear_mask(data_, c_typeMask)); }
const char* TheoryTerm::symbol() const {
    POTASSCO_CHECK(type() == Theory_t::Symbol, Errc::invalid_argument, "Term is not a symbol");
    return reinterpret_cast<const char*>(getPtr());
}
TheoryTerm::FuncData* TheoryTerm::func() const { return reinterpret_cast<FuncData*>(getPtr()); }
int                   TheoryTerm::compound() const {
    POTASSCO_CHECK(type() == Theory_t::Compound, Errc::invalid_argument, "Term is not a compound");
    return func()->base;
}
bool TheoryTerm::isFunction() const { return type() == Theory_t::Compound && func()->base >= 0; }
bool TheoryTerm::isTuple() const { return type() == Theory_t::Compound && func()->base < 0; }
Id_t TheoryTerm::function() const {
    POTASSCO_CHECK(isFunction(), Errc::invalid_argument, "Term is not a function");
    return static_cast<Id_t>(func()->base);
}
Tuple_t TheoryTerm::tuple() const {
    POTASSCO_CHECK(isTuple(), Errc::invalid_argument, "Term is not a tuple");
    return static_cast<Tuple_t>(func()->base);
}
uint32_t             TheoryTerm::size() const { return type() == Theory_t::Compound ? func()->size : 0; }
TheoryTerm::iterator TheoryTerm::begin() const { return type() == Theory_t::Compound ? func()->args : nullptr; }
TheoryTerm::iterator TheoryTerm::end() const {
    return type() == Theory_t::Compound ? func()->args + func()->size : nullptr;
}
TheoryElement::TheoryElement(const IdSpan& terms, const Id_t* c)
    : nTerms_(static_cast<uint32_t>(terms.size()))
    , nCond_(c != nullptr) {
    std::ranges::copy(terms, term_);
    if (c) {
        term_[nTerms_] = *c;
    }
}
Id_t TheoryElement::condition() const { return nCond_ == 0 ? 0 : term_[nTerms_]; }
void TheoryElement::setCondition(Id_t c) { term_[nTerms_] = c; }

TheoryAtom::TheoryAtom(Id_t a, Id_t term, const IdSpan& args, const Id_t* op, const Id_t* rhs)
    : atom_(a)
    , guard_(op != nullptr)
    , termId_(term)
    , nTerms_(static_cast<uint32_t>(args.size())) {
    std::ranges::copy(args, term_);
    if (op) {
        term_[nTerms_]     = *op;
        term_[nTerms_ + 1] = *rhs;
    }
}

const Id_t* TheoryAtom::guard() const { return guard_ != 0 ? &term_[nTerms_] : nullptr; }
const Id_t* TheoryAtom::rhs() const { return guard_ != 0 ? &term_[nTerms_ + 1] : nullptr; }
//////////////////////////////////////////////////////////////////////////////////////////////////////
// TheoryData
//////////////////////////////////////////////////////////////////////////////////////////////////////
struct TheoryData::DestroyT {
    template <class T>
    void operator()(T* x) const {
        if (x) {
            x->~T();
            ::operator delete(x);
        }
    }
    void operator()(TheoryTerm& raw) const {
        if (raw.data_ != c_nulTerm) {
            // TheoryTerm term(raw);
            if (auto type = raw.type(); type == Theory_t::Compound) {
                (*this)(raw.func());
            }
            else if (type == Theory_t::Symbol) {
                delete[] const_cast<char*>(raw.symbol());
            }
        }
    }
};
struct TheoryData::Data {
    static_assert(amc::is_trivially_relocatable_v<TheoryTerm>);
    template <typename T, typename... Args>
    static T* allocConstruct(Args&&... args) {
        auto bytes = (sizeof(T) + ... + computeExtraBytes(args));
        return new (::operator new(bytes)) T(std::forward<Args>(args)...);
    }
    static char* allocCString(std::string_view in) {
        auto str                              = new char[in.size() + 1];
        *std::copy(in.begin(), in.end(), str) = 0;
        return str;
    }
    amc::vector<TheoryAtom*>    atoms;
    amc::vector<TheoryElement*> elems;
    amc::vector<TheoryTerm>     terms;
    struct Up {
        Up() : atom(0), term(0), elem(0) {}
        uint32_t atom;
        uint32_t term;
        uint32_t elem;
    } frame;
};
TheoryData::TheoryData() : data_(std::make_unique<Data>()) {}
TheoryData::~TheoryData() { reset(); }
void TheoryData::addTerm(Id_t termId, int number) {
    auto& term = setTerm(termId);
    term       = (static_cast<uint64_t>(number) << 2) | uint32_t(Theory_t::Number);
}
void TheoryData::addTerm(Id_t termId, const std::string_view& name) {
    auto& term = setTerm(termId);
    term       = assertPtr(Data::allocCString(name), uint32_t(Theory_t::Symbol));
    POTASSCO_DEBUG_ASSERT(getTerm(termId).symbol() == name);
}
void TheoryData::addTerm(Id_t termId, const char* name) {
    return addTerm(termId, {name, name ? std::strlen(name) : 0});
}

void TheoryData::addTerm(Id_t termId, Id_t funcId, const IdSpan& args) {
    using FD   = TheoryTerm::FuncData;
    auto& term = setTerm(termId);
    term       = assertPtr(Data::allocConstruct<FD>(static_cast<int32_t>(funcId), args), uint32_t(Theory_t::Compound));
}
void TheoryData::addTerm(Id_t termId, Tuple_t type, const IdSpan& args) {
    using FD   = TheoryTerm::FuncData;
    auto& term = setTerm(termId);
    term       = assertPtr(Data::allocConstruct<FD>(static_cast<int32_t>(type), args), uint32_t(Theory_t::Compound));
}
void TheoryData::removeTerm(Id_t termId) {
    if (hasTerm(termId)) {
        DestroyT()(data_->terms[termId]);
        data_->terms[termId] = c_nulTerm;
    }
}
void TheoryData::addElement(Id_t id, const IdSpan& terms, Id_t cId) {
    if (not hasElement(id)) {
        data_->elems.resize(std::max(numTerms(), id + 1));
    }
    else {
        POTASSCO_CHECK_PRE(not isNewElement(id), "Redefinition of theory element '%u'", id);
        DestroyT()(data_->elems[id]);
    }
    data_->elems[id] = Data::allocConstruct<TheoryElement>(terms, cId != 0 ? &cId : nullptr);
}

void TheoryData::addAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elems) {
    data_->atoms.push_back(Data::allocConstruct<TheoryAtom>(atomOrZero, termId, elems, nullptr, nullptr));
}
void TheoryData::addAtom(Id_t atomOrZero, Id_t termId, const IdSpan& elems, Id_t op, Id_t rhs) {
    data_->atoms.push_back(Data::allocConstruct<TheoryAtom>(atomOrZero, termId, elems, &op, &rhs));
}

TheoryTerm& TheoryData::setTerm(Id_t id) {
    if (not hasTerm(id)) {
        data_->terms.resize(std::max(numTerms(), id + 1), c_nulTerm);
    }
    else {
        POTASSCO_CHECK_PRE(not isNewTerm(id), "Redefinition of theory term '%u'", id);
        removeTerm(id);
    }
    return data_->terms[id];
}
void TheoryData::setCondition(Id_t elementId, Id_t newCond) {
    POTASSCO_CHECK_PRE(getElement(elementId).condition() == COND_DEFERRED);
    data_->elems[elementId]->setCondition(newCond);
}

void TheoryData::reset() {
    DestroyT destroy;
    std::ranges::for_each(std::exchange(data_->atoms, {}), destroy);
    std::ranges::for_each(std::exchange(data_->elems, {}), destroy);
    std::ranges::for_each(std::exchange(data_->terms, {}), destroy);
    data_->frame = Data::Up();
}
void TheoryData::update() {
    data_->frame.atom = numAtoms();
    data_->frame.term = numTerms();
    data_->frame.elem = numElems();
}
uint32_t                  TheoryData::numAtoms() const { return data_->atoms.size(); }
uint32_t                  TheoryData::numTerms() const { return data_->terms.size(); }
uint32_t                  TheoryData::numElems() const { return data_->elems.size(); }
void                      TheoryData::resizeAtoms(uint32_t newSize) { data_->atoms.resize(newSize); }
void                      TheoryData::destroyAtom(Potassco::TheoryAtom* atom) { DestroyT{}(atom); }
TheoryData::atom_iterator TheoryData::begin() const { return data_->atoms.begin(); }
TheoryData::atom_iterator TheoryData::currBegin() const { return begin() + data_->frame.atom; }
TheoryData::atom_iterator TheoryData::end() const { return begin() + numAtoms(); }
bool       TheoryData::hasTerm(Id_t id) const { return id < numTerms() && data_->terms[id].data_ != c_nulTerm; }
bool       TheoryData::isNewTerm(Id_t id) const { return hasTerm(id) && id >= data_->frame.term; }
bool       TheoryData::hasElement(Id_t id) const { return id < numElems() && data_->elems[id] != nullptr; }
bool       TheoryData::isNewElement(Id_t id) const { return hasElement(id) && id >= data_->frame.elem; }
TheoryTerm TheoryData::getTerm(Id_t id) const {
    POTASSCO_CHECK(hasTerm(id), Errc::out_of_range, "Unknown term '%u'", unsigned(id));
    return data_->terms[id];
}
const TheoryElement& TheoryData::getElement(Id_t id) const {
    POTASSCO_CHECK(hasElement(id), Errc::out_of_range, "Unknown element '%u'", unsigned(id));
    return *data_->elems[id];
}
void TheoryData::accept(Visitor& out, VisitMode m) const {
    for (atom_iterator aIt = m == visit_current ? currBegin() : begin(), aEnd = end(); aIt != aEnd; ++aIt) {
        out.visit(*this, **aIt);
    }
}
void TheoryData::accept(const TheoryTerm& t, Visitor& out, VisitMode m) const {
    if (t.type() == Theory_t::Compound) {
        for (auto id : t) {
            if (doVisitTerm(m, id))
                out.visit(*this, id, getTerm(id));
        }
        if (t.isFunction() && doVisitTerm(m, t.function())) {
            out.visit(*this, t.function(), getTerm(t.function()));
        }
    }
}
void TheoryData::accept(const TheoryElement& e, Visitor& out, VisitMode m) const {
    for (auto id : e) {
        if (doVisitTerm(m, id)) {
            out.visit(*this, id, getTerm(id));
        }
    }
}
void TheoryData::accept(const TheoryAtom& a, Visitor& out, VisitMode m) const {
    if (doVisitTerm(m, a.term())) {
        out.visit(*this, a.term(), getTerm(a.term()));
    }
    for (auto id : a) {
        if (doVisitElem(m, id)) {
            out.visit(*this, id, getElement(id));
        }
    }
    if (a.guard() && doVisitTerm(m, *a.guard())) {
        out.visit(*this, *a.guard(), getTerm(*a.guard()));
    }
    if (a.rhs() && doVisitTerm(m, *a.rhs())) {
        out.visit(*this, *a.rhs(), getTerm(*a.rhs()));
    }
}

} // namespace Potassco
