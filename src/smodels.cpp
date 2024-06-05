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
#include <potassco/smodels.h>

#include <potassco/error.h>
#include <potassco/rule_utils.h>

#include <charconv>
#include <cstring>
#include <ostream>
#include <unordered_map>

namespace Potassco {
using namespace std::literals;
enum SmodelsRule : int {
    End             = 0,
    Basic           = 1,
    Cardinality     = 2,
    Choice          = 3,
    Generate        = 4,
    Weight          = 5,
    Optimize        = 6,
    Disjunctive     = 8,
    ClaspIncrement  = 90,
    ClaspAssignExt  = 91,
    ClaspReleaseExt = 92
};
int isSmodelsHead(Head_t t, const AtomSpan& head, bool allowConstraint) {
    if (head.empty()) {
        return allowConstraint && t != Head_t::Choice ? Basic : End;
    }
    if (t == Head_t::Choice) {
        return Choice;
    }
    return head.size() == 1 ? Basic : Disjunctive;
}

int isSmodelsRule(Head_t t, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body, bool allowConstraint) {
    if (isSmodelsHead(t, head, allowConstraint) != Basic || bound < 0) {
        return End;
    }
    auto ret = Cardinality;
    for (const auto& wl : body) {
        if (auto w = weight(wl); w < 0)
            return End;
        else if (w != 1)
            ret = Weight;
    }
    return ret;
}
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsInput
/////////////////////////////////////////////////////////////////////////////////////////
struct SmodelsInput::StringTab {
    bool addUnique(const std::string_view& name, Id_t id) { return map.try_emplace(name, id).second; }
    Id_t tryAdd(const std::string_view& name, Id_t id) {
        auto it = map.find(name);
        return it != map.end() ? it->second : map.emplace_hint(it, name, id)->second;
    }
    [[nodiscard]] Id_t findOr(const std::string_view& name, Id_t orVal) const {
        if (auto it = map.find(name); it != map.end()) {
            return it->second;
        }
        return orVal;
    }
    [[nodiscard]] uint32_t size() const noexcept { return static_cast<uint32_t>(map.size()); }
    using StringMap = std::unordered_map<FixedString, Id_t, std::hash<FixedString>, std::equal_to<>>;
    StringMap map;
};

SmodelsInput::SmodelsInput(AbstractProgram& out, const Options& opts, AtomLookup lookup)
    : out_(out)
    , lookup_(std::move(lookup))
    , opts_(opts) {
    if (opts_.cEdge) {
        nodes_ = std::make_unique<StringTab>();
    }
    if (opts_.cHeuristic && not lookup_) {
        atoms_  = std::make_unique<StringTab>();
        lookup_ = [this](std::string_view name) { return atoms_->findOr(name, 0); };
    }
}
SmodelsInput::~SmodelsInput() = default;
void SmodelsInput::doReset() {}
bool SmodelsInput::doAttach(bool& inc) {
    if (auto n = peek(); BufferedStream::isDigit(n) && ((inc = (n == '9')) == false || opts_.claspExt)) {
        out_.initProgram(inc);
        return true;
    }
    return false;
}

bool SmodelsInput::doParse() {
    out_.beginStep();
    if (readRules() && readSymbols() && readCompute() && readExtra()) {
        out_.endStep();
        return true;
    }
    return false;
}

void SmodelsInput::matchBody(RuleBuilder& rule) {
    auto len = matchUint();
    auto neg = matchUint();
    for (rule.startBody(); len--;) {
        Lit_t p = lit(matchAtom());
        if (neg) {
            p *= -1;
            --neg;
        }
        rule.addGoal(p);
    }
}

void SmodelsInput::matchSum(RuleBuilder& rule, bool weights) {
    auto bnd = matchUint();
    auto len = matchUint();
    auto neg = matchUint();
    if (not weights) {
        std::swap(len, bnd);
        std::swap(bnd, neg);
    }
    rule.startSum(static_cast<Weight_t>(bnd));
    for (uint32_t i = 0; i != len; ++i) {
        auto p = lit(matchAtom());
        if (neg) {
            p *= -1;
            --neg;
        }
        rule.addGoal(p);
    }
    if (weights) {
        for (auto *x = rule.wlits_begin(), *end = x + len; x != end; ++x) {
            x->weight = matchWeight(true, "non-negative weight expected");
        }
    }
}

bool SmodelsInput::readRules() {
    RuleBuilder rule;
    Weight_t    minPrio = 0;
    for (unsigned rt; (rt = matchId("rule type expected")) != 0;) {
        rule.clear();
        switch (rt) {
            default: error("unrecognized rule type"); return false;
            case Choice:
            case Disjunctive: // n a1..an
                rule.start(rt == Choice ? Head_t::Choice : Head_t::Disjunctive);
                for (unsigned i = matchAtom("positive head size expected"); i--;) { rule.addHead(matchAtom()); }
                matchBody(rule);
                rule.end(&out_);
                break;
            case Basic:
                rule.start(Head_t::Disjunctive).addHead(matchAtom());
                matchBody(rule);
                rule.end(&out_);
                break;
            case Cardinality: // fall through
            case Weight:      // fall through
                rule.start(Head_t::Disjunctive).addHead(matchAtom());
                matchSum(rule, rt == Weight);
                rule.end(&out_);
                break;
            case Optimize:
                rule.startMinimize(minPrio++);
                matchSum(rule, true);
                rule.end(&out_);
                break;
            case ClaspIncrement: require(opts_.claspExt && matchId() == 0, "unrecognized rule type"); break;
            case ClaspAssignExt:
            case ClaspReleaseExt:
                require(opts_.claspExt, "unrecognized rule type");
                if (rt == ClaspAssignExt) {
                    auto rHead = matchAtom();
                    out_.external(rHead, static_cast<Value_t>((matchUint(0u, 2u, "0..2 expected") ^ 3) - 1));
                }
                else {
                    out_.external(matchAtom(), Value_t::Release);
                }
                break;
        }
    }
    return true;
}

bool SmodelsInput::readSymbols() {
    std::string_view n0, n1;
    DynamicBuffer    scratch;
    auto             heuType = Heuristic_t::Init;
    auto             bias    = 0;
    auto             prio    = 0u;
    struct Deferred {
        constexpr bool setAtom(Atom_t a) { return a && ((sizeOrId = a) == a) && (atom = 1); }
        Lit_t          cond;
        int32_t        bias;
        uint32_t       prio;
        uint32_t       type     : 3;
        uint32_t       atom     : 1;
        uint32_t       sizeOrId : 28;
    };
    static constexpr auto c_defSize = sizeof(Deferred);
    static_assert(c_defSize == 16);
    DynamicBuffer deferredDom;

    for (Lit_t atom; (atom = (Lit_t) matchAtomOrZero()) != 0;) {
        scratch.clear();
        matchChar(' ');
        for (char c; (c = get()) != '\n';) {
            require(c != 0, "atom name expected!");
            scratch.push(c);
        }
        scratch.push(0);
        auto name   = scratch.view(0, scratch.size() - 1);
        auto filter = false;
        if (opts_.cEdge && matchEdgePred(name, n0, n1)) {
            auto s = (int) nodes_->tryAdd(n0, nodes_->size());
            auto t = (int) nodes_->tryAdd(n1, nodes_->size());
            out_.acycEdge(s, t, {&atom, 1});
            filter = opts_.filter;
        }
        else if (opts_.cHeuristic && matchDomHeuPred(name, n0, heuType, bias, prio)) {
            auto type = static_cast<uint32_t>(heuType);
            auto def  = Deferred{.cond = atom, .bias = bias, .prio = prio, .type = type, .atom = 0, .sizeOrId = 0};
            if (def.setAtom(lookup_(n0))) {
                std::memcpy(deferredDom.alloc(c_defSize).data(), &def, c_defSize);
            }
            else { // atom n0 not (yet) seen - lookup again later
                POTASSCO_CHECK_PRE((def.sizeOrId = n0.size()) == n0.size(), "Name too long");
                auto space = deferredDom.alloc(c_defSize + n0.size());
                std::memcpy(space.data(), &def, c_defSize);
                std::memcpy(space.data() + c_defSize, n0.data(), n0.size());
            }
            filter = opts_.filter;
        }
        if (not filter) {
            out_.output(name, {&atom, 1});
        }
        if (atoms_) {
            POTASSCO_CHECK_PRE(atoms_->addUnique(name, Potassco::atom(atom)), "Redefinition: atom '%s' already exists",
                               scratch.data());
        }
    }
    for (auto dom = deferredDom.view(); dom.size() > c_defSize;) {
        Deferred data;
        std::memcpy(&data, dom.data(), c_defSize);
        dom.remove_prefix(c_defSize);
        auto atomId = data.sizeOrId;
        if (not data.atom) {
            auto name = std::string_view{dom.data(), data.sizeOrId};
            POTASSCO_ASSERT(dom.size() >= data.sizeOrId);
            dom.remove_prefix(data.sizeOrId);
            atomId = lookup_(name);
        }
        if (atomId) {
            out_.heuristic(atomId, static_cast<Heuristic_t>(data.type), data.bias, data.prio, {&data.cond, 1});
        }
    }

    if (not incremental()) {
        nodes_.reset();
        atoms_.reset();
    }
    return true;
}

bool SmodelsInput::readCompute() {
    for (auto [part, pos] : {std::pair{"B+"sv, true}, std::pair{"B-"sv, false}}) {
        require(skipWs() && match(part), "compute statement expected");
        matchChar('\n');
        for (Lit_t x; (x = (Lit_t) matchAtomOrZero()) != 0;) {
            if (pos) {
                x = neg(x);
            }
            out_.rule(Head_t::Disjunctive, {}, {&x, 1});
        }
    }
    return true;
}

bool SmodelsInput::readExtra() {
    if (skipWs() && match("E"sv)) {
        for (Atom_t atom; (atom = matchAtomOrZero()) != 0;) { out_.external(atom, Value_t::Free); }
    }
    matchUint("number of models expected");
    return true;
}

int readSmodels(std::istream& in, AbstractProgram& out, const SmodelsInput::Options& opts) {
    SmodelsInput reader(out, opts);
    return readProgram(in, reader);
}
/////////////////////////////////////////////////////////////////////////////////////////
// String matching
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr auto heuristicPred = "_heuristic("sv;
static constexpr auto edgePred      = "_edge("sv;
static constexpr auto acycPred      = "_acyc_"sv;
static constexpr bool match(std::string_view& in, std::string_view word) {
    if (in.starts_with(word)) {
        in.remove_prefix(word.size());
        return true;
    }
    return false;
}
static constexpr bool match(std::string_view& in, char sep) { return match(in, {&sep, 1}); }

static bool matchNum(std::string_view& in, std::string_view* sOut, int* nOut = nullptr) {
    int  n;
    auto r  = std::from_chars(in.data(), in.data() + in.size(), nOut ? *nOut : n);
    auto sz = static_cast<std::size_t>(r.ptr - in.data());
    if (r.ec != std::errc{} || sz == 0)
        return false;
    if (sOut)
        *sOut = in.substr(0, sz);
    in.remove_prefix(sz);
    return true;
}

static bool match(std::string_view& input, Heuristic_t& heuType) {
    for (const auto& [k, n] : enum_entries<Heuristic_t>()) {
        if (not n.empty() && match(input, n)) {
            heuType = static_cast<Heuristic_t>(k);
            return true;
        }
    }
    return false;
}

bool matchEdgePred(std::string_view in, std::string_view& n0, std::string_view& n1) {
    if (match(in, acycPred)) { // _acyc_<ignore>_<n0>_<n1>
        return matchNum(in, nullptr) && match(in, '_') && matchNum(in, &n0) && match(in, '_') && matchNum(in, &n1) &&
               in.empty();
    }
    else if (match(in, edgePred)) { // _edge(<n0>,<n1>)
        return matchTerm(in, n0) && match(in, ',') && matchTerm(in, n1) && match(in, ')') && in.empty();
    }
    return false;
}
bool matchDomHeuPred(std::string_view in, std::string_view& atom, Heuristic_t& type, int& bias, unsigned& prio) {
    // _heuristic(<atom>,<type>,<bias>[,<prio>])
    if (match(in, heuristicPred) && matchTerm(in, atom) && match(in, ',') && match(in, type) && match(in, ',') &&
        matchNum(in, nullptr, &bias)) {
        prio = bias < 0 ? static_cast<unsigned>(~bias) + 1u : static_cast<unsigned>(bias);
        if (match(in, ',')) {
            if (int p; not matchNum(in, nullptr, &p) || p < 0) {
                return false;
            }
            else {
                prio = static_cast<unsigned>(p);
            }
        }
        return match(in, ')') && in.empty();
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////////
// SmodelsOutput
/////////////////////////////////////////////////////////////////////////////////////////
static constexpr Lit_t smLit(const WeightLit_t& x) { return x.weight >= 0 ? x.lit : -x.lit; }
static constexpr Lit_t smLit(Lit_t x) { return x; }
template <typename T>
static constexpr unsigned negSize(const std::span<T>& lits) {
    unsigned r = 0;
    for (const auto& x : lits) { r += smLit(x) < 0; }
    return r;
}

template <typename T, typename Op>
static void print(std::ostream& os, const std::span<T>& span, unsigned neg, unsigned pos, Op op) {
    for (auto it = span.begin(); neg; ++it) {
        if (smLit(*it) < 0) {
            os << " " << op(*it);
            --neg;
        }
    }
    for (auto it = span.begin(); pos; ++it) {
        if (smLit(*it) >= 0) {
            os << " " << op(*it);
            --pos;
        }
    }
}
template <typename T>
static void print(std::ostream& os, const std::span<T>& span, unsigned neg, unsigned pos) {
    print(os, span, neg, pos, [](auto x) { return atom(x); });
}
SmodelsOutput::SmodelsOutput(std::ostream& os, bool ext, Atom_t fAtom)
    : os_(os)
    , false_(fAtom)
    , sec_(0)
    , ext_(ext)
    , inc_(false)
    , fHead_(false) {}
SmodelsOutput& SmodelsOutput::startRule(int rt) {
    POTASSCO_CHECK_PRE(sec_ == 0 || rt == End || rt >= ClaspIncrement, "adding rules after symbols not supported");
    os_ << rt;
    return *this;
}
SmodelsOutput& SmodelsOutput::add(unsigned i) {
    os_ << " " << i;
    return *this;
}
SmodelsOutput& SmodelsOutput::add(Head_t ht, const AtomSpan& head) {
    if (head.empty()) {
        POTASSCO_CHECK_PRE(false_ != 0 && ht == Head_t::Disjunctive, "empty head requires false atom");
        fHead_ = true;
        return add(false_);
    }
    if (ht == Head_t::Choice || head.size() > 1) {
        add((unsigned) head.size());
    }
    for (auto atom : head) { add(atom); }
    return *this;
}

SmodelsOutput& SmodelsOutput::add(const LitSpan& lits) {
    unsigned neg = negSize(lits), size = static_cast<unsigned>(lits.size());
    add(size).add(neg);
    print(os_, lits, neg, size - neg);
    return *this;
}
SmodelsOutput& SmodelsOutput::add(Weight_t bnd, const WeightLitSpan& lits, bool card) {
    unsigned neg = negSize(lits), size = static_cast<unsigned>(lits.size());
    if (not card) {
        add(static_cast<unsigned>(bnd));
    }
    add(size).add(neg);
    if (card) {
        add(static_cast<unsigned>(bnd));
    }
    print(os_, lits, neg, size - neg);
    if (not card) {
        print(os_, lits, neg, size - neg, [](const WeightLit_t& wl) { return wl.weight; });
    }
    return *this;
}
SmodelsOutput& SmodelsOutput::endRule() {
    os_ << '\n';
    return *this;
}
void SmodelsOutput::initProgram(bool b) {
    POTASSCO_CHECK_PRE(not b || ext_, "incremental programs not supported in smodels format");
    inc_ = b;
}
void SmodelsOutput::beginStep() {
    sec_   = 0;
    fHead_ = false;
    if (ext_ && inc_) {
        startRule(ClaspIncrement).add(0).endRule();
    }
}
void SmodelsOutput::rule(Head_t ht, const AtomSpan& head, const LitSpan& body) {
    if (head.empty() && ht == Head_t::Choice)
        return;
    auto rt = (SmodelsRule) isSmodelsHead(ht, head, false_ != 0);
    POTASSCO_CHECK_PRE(rt != End, "unsupported rule type");
    startRule(rt).add(ht, head).add(body).endRule();
}
void SmodelsOutput::rule(Head_t ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (head.empty() && ht == Head_t::Choice)
        return;
    auto rt = (SmodelsRule) isSmodelsRule(ht, head, bound, body, false_ != 0);
    POTASSCO_CHECK_PRE(rt != End, "unsupported rule type");
    startRule(rt).add(ht, head).add(bound, body, rt == Cardinality).endRule();
}
void SmodelsOutput::minimize(Weight_t, const WeightLitSpan& lits) { startRule(Optimize).add(0, lits, false).endRule(); }
void SmodelsOutput::output(const std::string_view& str, const LitSpan& cond) {
    POTASSCO_CHECK_PRE(sec_ <= 1, "adding symbols after compute not supported");
    POTASSCO_CHECK_PRE(cond.size() == 1 && lit(cond.front()) > 0,
                       "general output directive not supported in smodels format");
    if (sec_ == 0) {
        startRule(End).endRule();
        sec_ = 1;
    }
    os_ << unsigned(cond[0]) << " ";
    os_.write(str.data(), std::ssize(str));
    os_ << '\n';
}
void SmodelsOutput::external(Atom_t a, Value_t t) {
    POTASSCO_CHECK_PRE(ext_, "external directive not supported in smodels format");
    if (t != Value_t::Release) {
        startRule(ClaspAssignExt).add(a).add((unsigned(t) ^ 3) - 1).endRule();
    }
    else {
        startRule(ClaspReleaseExt).add(a).endRule();
    }
}
void SmodelsOutput::assume(const LitSpan& lits) {
    POTASSCO_CHECK_PRE(sec_ < 2, "at most one compute statement supported in smodels format");
    while (sec_ != 2) {
        startRule(End).endRule();
        ++sec_;
    }
    os_ << "B+\n";
    for (auto x : lits) {
        if (lit(x) > 0) {
            os_ << atom(x) << '\n';
        }
    }
    os_ << "0\nB-\n";
    for (auto x : lits) {
        if (lit(x) < 0) {
            os_ << atom(x) << '\n';
        }
    }
    if (fHead_ && false_) {
        os_ << false_ << '\n';
    }
    os_ << "0\n";
}
void SmodelsOutput::endStep() {
    if (sec_ < 2) {
        SmodelsOutput::assume({});
    }
    os_ << "1\n";
}
} // namespace Potassco
