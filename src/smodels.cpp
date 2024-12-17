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
    [[nodiscard]] uint32_t size() const noexcept { return size_cast<uint32_t>(map); }
    using StringMap = std::unordered_map<ConstString, Id_t, std::hash<ConstString>, std::equal_to<>>;
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
    if (auto n = peek(); BufferedStream::isDigit(n) && (n != '9' || opts_.claspExt)) {
        inc = n == '9';
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
        for (auto& [_, weight] : rule.sumLits()) { weight = matchWeight(true, "non-negative weight expected"); }
    }
}

bool SmodelsInput::readRules() {
    RuleBuilder rule;
    Weight_t    minPrio = 0;
    for (SmodelsType rt; (rt = matchEnum<SmodelsType>("rule type expected")) != SmodelsType::end;) {
        rule.clear();
        switch (rt) {
            default: error("unrecognized rule type"); return false;
            case SmodelsType::choice:
            case SmodelsType::disjunctive: // n a1...an
                rule.start(rt == SmodelsType::choice ? HeadType::choice : HeadType::disjunctive);
                for (unsigned i = matchAtom("positive head size expected"); i--;) { rule.addHead(matchAtom()); }
                matchBody(rule);
                rule.end(&out_);
                break;
            case SmodelsType::basic:
                rule.start(HeadType::disjunctive).addHead(matchAtom());
                matchBody(rule);
                rule.end(&out_);
                break;
            case SmodelsType::cardinality: // fall through
            case SmodelsType::weight:      // fall through
                rule.start(HeadType::disjunctive).addHead(matchAtom());
                matchSum(rule, rt == SmodelsType::weight);
                rule.end(&out_);
                break;
            case SmodelsType::optimize:
                rule.startMinimize(minPrio++);
                matchSum(rule, true);
                rule.end(&out_);
                break;
            case SmodelsType::clasp_increment:
                require(opts_.claspExt && matchId() == 0, "unrecognized rule type");
                break;
            case SmodelsType::clasp_assign_ext:
            case SmodelsType::clasp_release_ext:
                require(opts_.claspExt, "unrecognized rule type");
                if (rt == SmodelsType::clasp_assign_ext) {
                    auto rHead = matchAtom();
                    out_.external(rHead, static_cast<TruthValue>((matchUint(0u, 2u, "0..2 expected") ^ 3) - 1));
                }
                else {
                    out_.external(matchAtom(), TruthValue::release);
                }
                break;
        }
    }
    return true;
}

bool SmodelsInput::readSymbols() {
    std::string_view n0, n1;
    DynamicBuffer    scratch;
    auto             heuType = DomModifier::init;
    auto             bias    = 0;
    auto             prio    = 0u;
    struct Deferred {
        constexpr bool setAtom(Atom_t a) {
            if (a) {
                sizeOrId = a;
                atom     = 1;
                return true;
            }
            return false;
        }
        Lit_t    cond;
        int32_t  bias;
        uint32_t prio;
        uint32_t type     : 3;
        uint32_t atom     : 1;
        uint32_t sizeOrId : 28;
    };
    static constexpr auto def_size = sizeof(Deferred);
    static_assert(def_size == 16);
    DynamicBuffer deferredDom;

    for (Lit_t atom; (atom = static_cast<Lit_t>(matchAtomOrZero())) != 0;) {
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
            auto s = static_cast<int>(nodes_->tryAdd(n0, nodes_->size()));
            auto t = static_cast<int>(nodes_->tryAdd(n1, nodes_->size()));
            out_.acycEdge(s, t, toSpan(atom));
            filter = opts_.filter;
        }
        else if (opts_.cHeuristic && matchDomHeuPred(name, n0, heuType, bias, prio)) {
            auto type = static_cast<uint32_t>(heuType);
            auto def  = Deferred{.cond = atom, .bias = bias, .prio = prio, .type = type, .atom = 0, .sizeOrId = 0};
            if (def.setAtom(lookup_(n0))) {
                std::memcpy(deferredDom.alloc(def_size).data(), &def, def_size);
            }
            else { // atom n0 not (yet) seen - lookup again later
                POTASSCO_CHECK_PRE((def.sizeOrId = n0.size()) == n0.size(), "Name too long");
                auto space = deferredDom.alloc(def_size + n0.size());
                std::memcpy(space.data(), &def, def_size);
                std::memcpy(space.data() + def_size, n0.data(), n0.size());
                if (opts_.filter && not atoms_) {
                    atoms_  = std::make_unique<StringTab>();
                    lookup_ = [this, prev = std::move(lookup_)](std::string_view aName) {
                        auto a = atoms_->findOr(aName, 0);
                        return a || not prev ? a : prev(aName);
                    };
                }
            }
            filter = opts_.filter;
        }
        if (not filter) {
            out_.output(name, toSpan(atom));
        }
        if (atoms_) {
            POTASSCO_CHECK_PRE(atoms_->addUnique(name, Potassco::atom(atom)), "Redefinition: atom '%s' already exists",
                               scratch.data());
        }
    }
    for (auto dom = deferredDom.view(); dom.size() >= def_size;) {
        Deferred data{};
        std::memcpy(&data, dom.data(), def_size);
        dom.remove_prefix(def_size);
        auto atomId = data.sizeOrId;
        if (not data.atom) {
            auto name = std::string_view{std::data(dom), data.sizeOrId};
            POTASSCO_ASSERT(dom.size() >= data.sizeOrId);
            dom.remove_prefix(data.sizeOrId);
            atomId = lookup_(name);
        }
        if (atomId) {
            out_.heuristic(atomId, static_cast<DomModifier>(data.type), data.bias, data.prio, toSpan(data.cond));
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
        for (Lit_t x; (x = static_cast<Lit_t>(matchAtomOrZero())) != 0;) {
            if (pos) {
                x = neg(x);
            }
            out_.rule(HeadType::disjunctive, {}, toSpan(x));
        }
    }
    return true;
}

bool SmodelsInput::readExtra() {
    if (skipWs() && match("E"sv)) {
        for (Atom_t atom; (atom = matchAtomOrZero()) != 0;) { out_.external(atom, TruthValue::free); }
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
static constexpr auto heuristic_pred = "_heuristic("sv;
static constexpr auto edge_pred      = "_edge("sv;
static constexpr auto acyc_pred      = "_acyc_"sv;
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
    if (r.ec != std::errc{} || sz == 0) {
        return false;
    }
    if (sOut) {
        *sOut = in.substr(0, sz);
    }
    in.remove_prefix(sz);
    return true;
}

static bool match(std::string_view& input, DomModifier& heuType) {
    for (const auto& [k, n] : enum_entries<DomModifier>()) {
        if (not n.empty() && match(input, n)) {
            heuType = static_cast<DomModifier>(k);
            return true;
        }
    }
    return false;
}

bool matchEdgePred(std::string_view in, std::string_view& n0, std::string_view& n1) {
    if (match(in, acyc_pred)) { // _acyc_<ignore>_<n0>_<n1>
        return matchNum(in, nullptr) && match(in, '_') && matchNum(in, &n0) && match(in, '_') && matchNum(in, &n1) &&
               in.empty();
    }
    else if (match(in, edge_pred)) { // _edge(<n0>,<n1>)
        return matchTerm(in, n0) && match(in, ',') && matchTerm(in, n1) && match(in, ')') && in.empty();
    }
    return false;
}
bool matchDomHeuPred(std::string_view in, std::string_view& atom, DomModifier& type, int& bias, unsigned& prio) {
    // _heuristic(<atom>,<type>,<bias>[,<prio>])
    if (match(in, heuristic_pred) && matchTerm(in, atom) && match(in, ',') && match(in, type) && match(in, ',') &&
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
static constexpr Lit_t smLit(const WeightLit& x) { return x.weight >= 0 ? x.lit : -x.lit; }
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
SmodelsOutput& SmodelsOutput::startRule(SmodelsType rt) {
    POTASSCO_CHECK_PRE(sec_ == 0 || rt == SmodelsType::end || rt >= SmodelsType::clasp_increment,
                       "adding rules after symbols not supported");
    os_ << to_underlying(rt);
    return *this;
}
SmodelsOutput& SmodelsOutput::add(unsigned i) {
    os_ << " " << i;
    return *this;
}
SmodelsOutput& SmodelsOutput::add(HeadType ht, const AtomSpan& head) {
    if (head.empty()) {
        POTASSCO_CHECK_PRE(false_ != 0 && ht == HeadType::disjunctive, "empty head requires false atom");
        fHead_ = true;
        return add(false_);
    }
    if (ht == HeadType::choice || head.size() > 1) {
        add(size_cast<unsigned>(head));
    }
    for (auto atom : head) { add(atom); }
    return *this;
}

SmodelsOutput& SmodelsOutput::add(const LitSpan& lits) {
    unsigned neg = negSize(lits), size = size_cast<unsigned>(lits);
    add(size).add(neg);
    print(os_, lits, neg, size - neg);
    return *this;
}
SmodelsOutput& SmodelsOutput::add(Weight_t bound, const WeightLitSpan& lits, bool card) {
    unsigned neg = negSize(lits), size = size_cast<unsigned>(lits);
    if (not card) {
        add(static_cast<unsigned>(bound));
    }
    add(size).add(neg);
    if (card) {
        add(static_cast<unsigned>(bound));
    }
    print(os_, lits, neg, size - neg);
    if (not card) {
        print(os_, lits, neg, size - neg, [](const WeightLit& wl) { return wl.weight >= 0 ? wl.weight : -wl.weight; });
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
        startRule(SmodelsType::clasp_increment).add(0).endRule();
    }
}
void SmodelsOutput::rule(HeadType ht, const AtomSpan& head, const LitSpan& body) {
    if (head.empty() && ht == HeadType::choice) {
        return;
    }
    POTASSCO_CHECK_PRE(false_ != 0 || not head.empty(), "empty head requires false atom");
    auto rt = ht == HeadType::choice ? SmodelsType::choice
              : head.size() > 1      ? SmodelsType::disjunctive
                                     : SmodelsType::basic;
    startRule(rt).add(ht, head).add(body).endRule();
}
void SmodelsOutput::rule(HeadType ht, const AtomSpan& head, Weight_t bound, const WeightLitSpan& body) {
    if (head.empty() && ht == HeadType::choice) {
        return;
    }
    POTASSCO_CHECK_PRE(ht == HeadType::disjunctive && head.size() < 2, "normal head expected");
    POTASSCO_CHECK_PRE(false_ != 0 || not head.empty(), "empty head requires false atom");
    bound   = std::max(bound, 0);
    auto rt = SmodelsType::cardinality;
    for (const auto& wl : body) {
        POTASSCO_CHECK_PRE(weight(wl) >= 0, "negative weights not supported");
        if (weight(wl) != 1) {
            rt = SmodelsType::weight;
        }
    }
    startRule(rt).add(ht, head).add(bound, body, rt == SmodelsType::cardinality).endRule();
}
void SmodelsOutput::minimize(Weight_t, const WeightLitSpan& lits) {
    startRule(SmodelsType::optimize).add(0, lits, false).endRule();
}
void SmodelsOutput::output(const std::string_view& str, const LitSpan& cond) {
    POTASSCO_CHECK_PRE(sec_ <= 1, "adding symbols after compute not supported");
    POTASSCO_CHECK_PRE(cond.size() == 1 && lit(cond.front()) > 0,
                       "general output directive not supported in smodels format");
    if (sec_ == 0) {
        startRule(SmodelsType::end).endRule();
        sec_ = 1;
    }
    os_ << static_cast<unsigned>(cond[0]) << " ";
    os_.write(std::data(str), std::ssize(str));
    os_ << '\n';
}
void SmodelsOutput::external(Atom_t a, TruthValue t) {
    POTASSCO_CHECK_PRE(ext_, "external directive not supported in smodels format");
    if (t != TruthValue::release) {
        startRule(SmodelsType::clasp_assign_ext).add(a).add((to_underlying(t) ^ 3) - 1).endRule();
    }
    else {
        startRule(SmodelsType::clasp_release_ext).add(a).endRule();
    }
}
void SmodelsOutput::assume(const LitSpan& lits) {
    POTASSCO_CHECK_PRE(sec_ < 2, "at most one compute statement supported in smodels format");
    while (sec_ != 2) {
        startRule(SmodelsType::end).endRule();
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
