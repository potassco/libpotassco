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
#include <potassco/clingo.h>

namespace Potassco {
AbstractAssignment::~AbstractAssignment() = default;
AbstractSolver::~AbstractSolver()         = default;
AbstractPropagator::~AbstractPropagator() = default;
AbstractHeuristic::~AbstractHeuristic()   = default;
AbstractStatistics::~AbstractStatistics() = default;
AbstractPropagator::Init::~Init()         = default;
auto AbstractPropagator::Init::addWatch(Lit_t lit) -> void { addWatch(lit, UINT32_MAX); }
auto AbstractPropagator::Init::removeWatch(Lit_t lit) -> void { removeWatch(lit, UINT32_MAX); }

auto AbstractAssignment::isTotal() const -> bool { return unassigned() == 0u; }
auto AbstractAssignment::isFixed(Lit_t lit) const -> bool { return value(lit) != TruthValue::free && level(lit) == 0; }
auto AbstractAssignment::isTrue(Lit_t lit) const -> bool { return value(lit) == TruthValue::true_; }
auto AbstractAssignment::isFalse(Lit_t lit) const -> bool { return value(lit) == TruthValue::false_; }
auto AbstractAssignment::trailEnd(uint32_t lev) const -> uint32_t {
    return lev < level() ? trailBegin(lev + 1) : trailSize();
}

} // namespace Potassco
