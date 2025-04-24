//
// Copyright (c) 2016 - present, Benjamin Kaufmann
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
#include "test_common.h"

#include <potassco/aspif_text.h>
#include <potassco/rule_utils.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

namespace Potassco::Test::Text {
namespace {
struct TextObserver : ReadObserver {
    void beginStep() override {
        ReadObserver::beginStep();
        os.str("");
    }
    void rule(HeadType ht, AtomSpan head, LitSpan body) override {
        write(ht, head, not body.empty() || (ht == HeadType::disjunctive && head.empty()));
        write(body) << ".\n";
    }
    void rule(HeadType ht, AtomSpan head, Weight_t bound, WeightLitSpan body) override {
        write(ht, head) << bound;
        write(body, " {", "}.\n");
    }
    void minimize(Weight_t prio, WeightLitSpan lits) override { write(lits, "#minimize {", "}@") << prio << ".\n"; }
    void project(AtomSpan pro) override { write(pro, "#project {", "}.\n"); }
    void external(Atom_t a, TruthValue v) override { write(toSpan(a), "#external {", "}. [") << enum_name(v) << "]\n"; }
    void assume(LitSpan lits) override { write(lits, "#assume {", "}.\n"); }
    void heuristic(Atom_t a, DomModifier t, int bias, unsigned prio, LitSpan condition) override {
        os << "#heuristic x_" << a << (condition.empty() ? "" : " : ");
        write(condition) << ". [" << bias << "@" << prio << ", " << enum_name(t) << "]\n";
    }
    void acycEdge(int s, int t, LitSpan condition) override {
        os << "#edge (" << s << "," << t << ")";
        write(condition, not condition.empty() ? " : " : "", ".\n");
    }
    void outputTerm(Id_t termId, std::string_view name) override { terms.try_emplace(termId, name); }
    void output(Id_t termId, LitSpan cond) override {
        auto it = terms.find(termId);
        os << "#term " << (it != terms.end() ? it->second.view() : "?");
        write(cond, not cond.empty() ? " : " : "") << ". [" << termId << "]\n";
    }

    [[nodiscard]] std::string program() const { return os.str(); }

    template <typename SpanT>
    std::stringstream& write(SpanT sp, const char* beg = "", const char* end = "", const char* sep = "; ") {
        os << beg;
        for (const char* s = ""; auto x : sp) {
            os << s << (lit(x) < 0 ? "not x_" : "x_") << atom(x);
            if constexpr (std::is_same_v<decltype(x), WeightLit>) {
                os << "=" << weight(x);
            }
            s = sep;
        }
        os << end;
        return os;
    }
    std::stringstream& write(HeadType ht, AtomSpan head, bool hasBody = true) {
        (ht == HeadType::choice ? write(head, "{", "}") : write(head)) << (hasBody ? " :- " : "");
        return os;
    }
    std::unordered_map<Id_t, ConstString> terms;
    std::stringstream                     os;
};
} // namespace
static bool read(ProgramReader& in, std::stringstream& str) { return in.accept(str) && in.parse(); }
TEST_CASE("Text reader ", "[text]") {
    std::stringstream input;
    TextObserver      observer;
    AspifTextInput    prg(&observer);
    SECTION("empty") {
        REQUIRE(read(prg, input));
        REQUIRE(observer.nStep == 1);
        REQUIRE_FALSE(observer.incremental);
        REQUIRE(observer.program().empty());
    }
    SECTION("empty integrity constraint") {
        input << ":- .";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == " :- .\n");
    }
    SECTION("fact") {
        input << "x1.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "x_1.\n");
    }
    SECTION("basic rule") {
        input << "x1 :- not   x2.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "x_1 :- not x_2.\n");
    }
    SECTION("choice rule") {
        input << "{x1} :- not x2.\n";
        input << "{x2, x3}.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "{x_1} :- not x_2.\n{x_2; x_3}.\n");
    }
    SECTION("empty choice rule") {
        input << "{}.\n";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "{}.\n");
    }
    SECTION("disjunctive rule") {
        input << "x1 | x2 :- not x3.";
        input << "x1 ; x2 :- not x4.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "x_1; x_2 :- not x_3.\nx_1; x_2 :- not x_4.\n");
    }
    SECTION("weight rule") {
        input << "x1 :- 2 {x2, x3=2, not x4 = 3, x5}.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "x_1 :- 2 {x_2=1; x_3=2; not x_4=3; x_5=1}.\n");
    }
    SECTION("alternative atom names") {
        input << "a :- not b, x_3.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "x_1 :- not x_2; x_3.\n");
    }
    SECTION("integrity constraint") {
        input << ":- x1, not x2.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == " :- x_1; not x_2.\n");
    }
    SECTION("minimize constraint") {
        input << "#minimize {x1, x2, x3}.\n";
        input << "#minimize {not x1=2, x4, not x5 = 3}@1.\n";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() ==
                "#minimize {x_1=1; x_2=1; x_3=1}@0.\n#minimize {not x_1=2; x_4=1; not x_5=3}@1.\n");
    }
    SECTION("output") {
        SECTION("implicit") {
            SECTION("atom condition maps to atom") {
                input << "#output a(1) : x1.\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program().empty());
                REQUIRE(observer.atoms.at(1) == "a(1)");
            }
            SECTION("empty condition maps to term") {
                input << "#output foo.\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term foo. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("negative literal maps to term") {
                input << "#output a(1) : not x1.\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term a(1) : not x_1. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("conjunction maps to term") {
                input << "#output a(1) : x1, x2.\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term a(1) : x_1; x_2. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("invalid atom name maps to term") {
                input << "#output \"A(X)\" : x1.\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term \"A(X)\" : x_1. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
        }
        SECTION("explicit term") {
            SECTION("empty condition") {
                input << "#output foo. [term]\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term foo. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("atom condition") {
                input << "#output a(1) : x1. [term]\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term a(1) : x_1. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("multiple conditions") {
                input << "#output a(1) : x1. [term]\n";
                input << "#output a(1) : x2. [term]\n";
                input << "#output a(1) : x3. [term]\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term a(1) : x_1. [0]\n"
                                              "#term a(1) : x_2. [0]\n"
                                              "#term a(1) : x_3. [0]\n");
                REQUIRE(observer.atoms.empty());
            }
            SECTION("multiple terms") {
                input << "#output a(1) : x1. [term]\n";
                input << "#output a(2) : x2. [term]\n";
                input << "#output a(3) : x3. [term]\n";
                REQUIRE(read(prg, input));
                REQUIRE(observer.program() == "#term a(1) : x_1. [0]\n"
                                              "#term a(2) : x_2. [1]\n"
                                              "#term a(3) : x_3. [2]\n");
                REQUIRE(observer.atoms.empty());
            }
        }
        SECTION("explicit invalid") {
            SECTION("unknown tag") {
                input << "#output a(1) : x1. [atom]\n";
                REQUIRE_THROWS_WITH(read(prg, input),
                                    Catch::Matchers::ContainsSubstring("parse error in line 1: 'term' expected"));
            }
            SECTION("unclosed tag") {
                input << "#output a(1) : x1. [term\n";
                REQUIRE_THROWS_WITH(read(prg, input),
                                    Catch::Matchers::ContainsSubstring("parse error in line 2: ']' expected"));
            }
        }
    }
    SECTION("project") {
        input << "#project {a,x2}.";
        input << "#project {}.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "#project {x_1; x_2}.\n"
                                      "#project {}.\n");
    }
    SECTION("external") {
        input << "#external x1.\n";
        input << "#external x2. [true]\n";
        input << "#external x3. [false]\n";
        input << "#external x4. [free]\n";
        input << "#external x5. [release]\n";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "#external {x_1}. [false]\n"
                                      "#external {x_2}. [true]\n"
                                      "#external {x_3}. [false]\n"
                                      "#external {x_4}. [free]\n"
                                      "#external {x_5}. [release]\n");
    }
    SECTION("external with unknown value") {
        input << "#external x2. [open]\n";
        REQUIRE_THROWS(read(prg, input));
    }
    SECTION("assume") {
        input << "#assume {a, not x2}.";
        input << "#assume {}.";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "#assume {x_1; not x_2}.\n"
                                      "#assume {}.\n");
    }
    SECTION("heuristic") {
        input << "#heuristic x1. [1, level]";
        input << "#heuristic x2 : x1. [2@1, true]";
        input << "#heuristic x3 :. [1,level]";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "#heuristic x_1. [1@0, level]\n"
                                      "#heuristic x_2 : x_1. [2@1, true]\n"
                                      "#heuristic x_3. [1@0, level]\n");
    }
    SECTION("edge") {
        input << "#edge (1,2) : x1.";
        input << "#edge (2,1).";
        REQUIRE(read(prg, input));
        REQUIRE(observer.program() == "#edge (1,2) : x_1.\n"
                                      "#edge (2,1).\n");
    }
    SECTION("read incremental") {
        input << "#incremental.\n";
        input << "{x1}.\n";
        input << "#step.\n";
        input << "{x2}.\n";
        REQUIRE(read(prg, input));
        REQUIRE(observer.incremental);
        REQUIRE(observer.nStep == 1);
        REQUIRE(observer.program() == "{x_1}.\n");
        REQUIRE(prg.parse());
        REQUIRE(observer.nStep == 2);
        REQUIRE(observer.program() == "{x_2}.\n");
    }
    SECTION("read error") {
        input << "#incremental.\n";
        input << "#foo.\n";
        REQUIRE_THROWS_WITH(read(prg, input),
                            Catch::Matchers::ContainsSubstring("parse error in line 2: unrecognized directive"));
    }
}
/////////////////////////////////////////////////////////////////////////////////////////
// AspifTextOutput
/////////////////////////////////////////////////////////////////////////////////////////
TEST_CASE("Text writer ", "[text]") {
    std::stringstream output;
    AspifTextOutput   out(output);
    RuleBuilder       rb;
    auto              end = [&]() {
        out.endStep();
        return output.str();
    };
    out.initProgram(false);
    out.beginStep();
    SECTION("empty program is empty") { REQUIRE(end().empty()); }
    SECTION("simple fact") {
        rb.addHead(1).end(&out);
        REQUIRE(end() == "x_1.\n#show.\n");
    }
    SECTION("named fact") {
        rb.addHead(1).end(&out);
        out.outputAtom(1u, "foo");
        REQUIRE(end() == "foo.\n");
    }
    SECTION("long named fact") {
        std::string longName(33, 'a');
        rb.addHead(1).end(&out);
        out.outputAtom(1u, longName);
        longName.push_back('.');
        longName.push_back('\n');
        REQUIRE(end() == longName);
    }
    SECTION("simple choice") {
        rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
        out.outputAtom(1, "foo");
        REQUIRE(end() == "{foo;x_2}.\n#show foo/0.\n");
    }
    SECTION("empty choice") {
        rb.start(HeadType::choice).end(&out);
        REQUIRE(end() == "{}.\n");
    }
    SECTION("integrity constraint") {
        rb.addGoal(1).addGoal(2).end(&out);
        out.outputAtom(1, "foo");
        REQUIRE(end() == ":- foo, x_2.\n#show foo/0.\n");
    }
    SECTION("empty integrity constraint") {
        rb.end(&out);
        REQUIRE(end() == ":- .\n");
    }
    SECTION("classical negation") {
        rb.start(HeadType::choice).addHead(1u).end(&out);
        out.outputAtom(1u, "-a");
        out.outputTerm(0u, "-8");
        out.output(0u, std::vector{static_cast<Lit_t>(1)});
        REQUIRE(end() == "{-a}.\n#show -8 : -a.\n");
    }
    SECTION("classical negation tricky") {
        rb.start(HeadType::choice).addHead(1u).addHead(2u).end(&out);
        out.outputAtom(1u, "-a");
        out.outputAtom(2u, "b");
        out.endStep();
        REQUIRE(end() == "{-a;b}.\n");
    }
    SECTION("basic rule") {
        rb.addHead(1u).addGoal(2).addGoal(-3).addGoal(4).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() == "foo :- x_2, not bar, x_4.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("choice rule") {
        rb.start(HeadType::choice).addHead(1u).addHead(2u).addGoal(-3).addGoal(4).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() == "{foo;x_2} :- not bar, x_4.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("disjunctive rule") {
        rb.start(HeadType::disjunctive).addHead(1u).addHead(2u).addGoal(-3).addGoal(4).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() == "foo|x_2 :- not bar, x_4.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("cardinality rule") {
        rb.start(HeadType::disjunctive).addHead(1u).addHead(2u).startSum(1).addGoal(-3).addGoal(4).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() == "foo|x_2 :- 1 #count{1 : not bar; 2 : x_4}.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("sum rule") {
        rb.addHead(1u).addHead(2u).startSum(3).addGoal(-3, 2).addGoal(4).addGoal(5, 1).addGoal(6, 2).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() ==
                "foo|x_2 :- 3 #sum{2,1 : not bar; 1,2 : x_4; 1,3 : x_5; 2,4 : x_6}.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("convert sum rule to cardinality rule") {
        rb.addHead(1u).addHead(2u).startSum(3).addGoal(-3, 2).addGoal(4, 2).addGoal(5, 2).addGoal(6, 2).end(&out);
        out.outputAtom(1, "foo");
        out.outputAtom(3, "bar");
        REQUIRE(end() == "foo|x_2 :- 2 #count{1 : not bar; 2 : x_4; 3 : x_5; 4 : x_6}.\n#show foo/0.\n#show bar/0.\n");
    }
    SECTION("convert sum rule with duplicate to cardinality rule") {
        rb.addHead(2u).startSum(3).addGoal(3, 1).addGoal(4, 1).addGoal(5, 1).addGoal(3, 1).end(&out);
        REQUIRE(end() == "x_2 :- 3 #count{1 : x_3; 2 : x_4; 3 : x_5; 4 : x_3}.\n#show.\n");
    }
    SECTION("minimize rule") {
        rb.startMinimize(0).addGoal(1).addGoal(2, 2).addGoal(3).end(&out);
        rb.startMinimize(1).addGoal(-1, 3).addGoal(-2).addGoal(-3).end(&out);
        REQUIRE(end() == "#minimize{1@0,1 : x_1; 2@0,2 : x_2; 1@0,3 : x_3}.\n#minimize{3@1,1 : not x_1; 1@1,2 : not "
                         "x_2; 1@1,3 : not x_3}.\n#show.\n");
    }
    SECTION("empty minimize") {
        out.minimize(0, {});
        REQUIRE(end() == "#minimize{0@0}.\n");
    }
    SECTION("external") {
        SECTION("false is default") {
            out.external(1, TruthValue::false_);
            REQUIRE(end() == "#external x_1.\n#show.\n");
            REQUIRE(end() == "#external x_1.\n#show.\n");
        }
        SECTION("with value") {
            out.external(1, TruthValue::true_);
            out.external(2, TruthValue::free);
            out.external(3, TruthValue::release);
            REQUIRE(end() == "#external x_1. [true]\n#external x_2. [free]\n#external x_3. [release]\n#show.\n");
        }
    }
    SECTION("assumption") {
        out.assume(Vec<Lit_t>{1, -2, 3});
        REQUIRE(end() == "#assume{x_1, not x_2, x_3}.\n#show.\n");
    }
    SECTION("empty assumption") {
        out.assume({});
        REQUIRE(end() == "#assume{}.\n");
    }
    SECTION("projection directive") {
        out.project(Vec<Atom_t>{1, 2, 3});
        REQUIRE(end() == "#project{x_1, x_2, x_3}.\n#show.\n");
    }
    SECTION("empty projection") {
        out.project({});
        REQUIRE(end() == "#project{}.\n");
    }
    SECTION("edge directive") {
        out.acycEdge(0, 1, Vec<Lit_t>{1, -2});
        out.acycEdge(1, 0, {});
        REQUIRE(end() == "#edge(0,1) : x_1, not x_2.\n#edge(1,0).\n#show.\n");
    }

    SECTION("heuristic directive -") {
        SECTION("simple") {
            out.heuristic(1, DomModifier::true_, 1, 0, {});
            REQUIRE(end() == "#heuristic x_1. [1, true]\n#show.\n");
        }
        SECTION("simple with priority") {
            out.heuristic(1, DomModifier::true_, 1, 2, {});
            REQUIRE(end() == "#heuristic x_1. [1@2, true]\n#show.\n");
        }
        SECTION("with condition") {
            out.heuristic(1, DomModifier::true_, 1, 2, Vec<Lit_t>{2, -3});
            REQUIRE(end() == "#heuristic x_1 : x_2, not x_3. [1@2, true]\n#show.\n");
        }
    }
    SECTION("incremental program") {
        out.initProgram(true);
        out.beginStep();
        rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
        out.external(3u, TruthValue::false_);
        out.outputAtom(1, "a(1)");
        out.endStep();
        out.beginStep();
        rb.start().addHead(3).addGoal(1).end(&out);
        out.endStep();
        out.beginStep();
        rb.start().addHead(4).addGoal(2).addGoal(-3).end(&out);
        REQUIRE(end() == "% #program base.\n"
                         "{a(1);x_2}.\n"
                         "#external x_3.\n"
                         "#show a/1.\n"
                         "% #program step(1).\n"
                         "x_3 :- a(1).\n"
                         "% #program step(2).\n"
                         "x_4 :- x_2, not x_3.\n");
    }

    SECTION("output") {
        SECTION("atom is used for lookup") {
            rb.start().addHead(1).end(&out);
            out.outputAtom(1, "a(1)");
            REQUIRE(end() == "a(1).\n");
        }
        SECTION("duplicate atom is shown as eq atom") {
            rb.start().addHead(1).end(&out);
            out.outputAtom(1, "a(1)");
            out.outputAtom(1, "b(1)");
            REQUIRE(end() == "b(1) :- a(1).\na(1).\n");
        }
        SECTION("duplicate atom name creates bogus output") {
            rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
            out.outputAtom(1, "a(1)");
            out.outputAtom(2, "a(1)");
            REQUIRE(end() == "{a(1);a(1)}.\n");
        }
        SECTION("bogus duplicate name is ignored") {
            rb.start(HeadType::choice).addHead(1).end(&out);
            out.outputAtom(1, "a(1)");
            out.outputAtom(1, "a(1)");
            REQUIRE(end() == "{a(1)}.\n");
        }
        SECTION("string is treated as term") {
            rb.start(HeadType::choice).addHead(1).end(&out);
            out.outputAtom(1, "\"Foo\"");
            REQUIRE(end() == "{x_1}.\n#show \"Foo\" : x_1.\n#show.\n");
        }
        SECTION("tuple is treated as term") {
            rb.start(HeadType::choice).addHead(1).end(&out);
            out.outputAtom(1, "(one,two)");
            REQUIRE(end() == "{x_1}.\n#show (one,two) : x_1.\n#show.\n");
        }
        SECTION("unknown is treated as term") {
            rb.start(HeadType::choice).addHead(1).end(&out);
            out.outputAtom(1, "A(x)");
            REQUIRE(end() == "{x_1}.\n#show A(x) : x_1.\n#show.\n");
        }
        SECTION("does not parse atom") {
            rb.start(HeadType::choice).addHead(1).end(&out);
            out.outputAtom(1, "a(X)");
            REQUIRE(end() == "{a(X)}.\n");
        }
        SECTION("term") {
            rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
            SECTION("no condition no output") {
                out.outputTerm(1, "a");
                REQUIRE(end() == "{x_1;x_2}.\n#show.\n");
            }
            SECTION("simple") {
                out.outputTerm(1, "a");
                out.output(1, Vec<Lit_t>{1});
                out.output(1, Vec<Lit_t>{2});
                REQUIRE(end() == "{x_1;x_2}.\n#show a : x_1.\n#show a : x_2.\n#show.\n");
            }
            SECTION("complex") {
                out.outputTerm(1, "a");
                out.outputAtom(1, "a");
                out.output(1, Vec<Lit_t>{1, 2});
                REQUIRE(end() == "{a;x_2}.\n#show a : a, x_2.\n#show a/0.\n");
            }
            SECTION("duplicate condition different term") {
                out.outputTerm(0, "a");
                out.outputTerm(1, "b");
                out.output(0, Vec<Lit_t>{1});
                out.output(1, Vec<Lit_t>{1});
                out.output(0, Vec<Lit_t>{2});
                REQUIRE(end() == "{x_1;x_2}.\n#show a : x_1.\n#show b : x_1.\n#show a : x_2.\n#show.\n");
            }
            SECTION("duplicate condition same term") {
                out.outputTerm(0, "a");
                out.output(0, Vec<Lit_t>{1});
                out.output(0, Vec<Lit_t>{1});
                REQUIRE(end() == "{x_1;x_2}.\n#show a : x_1.\n#show a : x_1.\n#show.\n");
            }
        }
        SECTION("explicit show if not all atoms shown") {
            rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
            SECTION("no atoms shown") { REQUIRE(end() == "{x_1;x_2}.\n#show.\n"); }
            SECTION("only some atoms shown") {
                out.outputAtom(1, "a(1)");
                REQUIRE(end() == "{a(1);x_2}.\n#show a/1.\n");
            }
        }
        SECTION("complex predicates") {
            rb.start(HeadType::choice).addHead(1).addHead(2).addHead(3).addHead(4).addHead(5).end(&out);
            out.outputAtom(1, "a");
            out.outputAtom(2, "a(1,2,3,4,5,6,7,8,9,10,11,12)");
            out.outputAtom(3, "b(t(1,2,3))");
            out.outputAtom(4, "b()");
            REQUIRE(end() == "{a;a(1,2,3,4,5,6,7,8,9,10,11,12);b(t(1,2,3));b;x_5}.\n#show a/0.\n#show "
                             "a/12.\n#show b/1.\n#show b/0.\n");
        }
        SECTION("reserved name") {
            rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
            SECTION("match all") {
                out.outputAtom(1, "x_1");
                out.outputAtom(2, "x_2");
                REQUIRE(end() == "{x_1;x_2}.\n");
            }
            SECTION("match some") {
                out.outputAtom(2, "x_2");
                REQUIRE(end() == "{x_1;x_2}.\n#show x_2/0.\n");
            }
            SECTION("mismatch") { REQUIRE_THROWS_AS(out.outputAtom(1, "x_2"), RuntimeError); }
            SECTION("set atom predicate") {
                out.initProgram(false);
                out.beginStep();
                rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
                SECTION("zero") {
                    out.setAtomPred("_a_");
                    out.outputAtom(1, "x_2");
                    REQUIRE(end() == "{x_2;_a_2}.\n#show x_2/0.\n");
                }
                SECTION("one") {
                    out.setAtomPred("_a/1");
                    out.outputAtom(1, "x_2");
                    REQUIRE(end() == "{x_2;_a(2)}.\n#show x_2/0.\n");
                }
                SECTION("invalid") {
                    REQUIRE_THROWS_AS(out.setAtomPred("_a/2"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_a/2/0"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("-_a"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_a("), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_a()"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_a(1)"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("_1"), std::logic_error);
                    REQUIRE_THROWS_AS(out.setAtomPred("Atom_"), std::logic_error);
                }
                SECTION("clash") {
                    out.setAtomPred("_a/1");
                    REQUIRE_THROWS_AS(out.outputAtom(1, "_a(2)"), RuntimeError);
                    REQUIRE_NOTHROW(out.outputAtom(1, "_a(1)"));
                }
            }
        }
        SECTION("invalid predicate") {
            SECTION("missing close") { REQUIRE_THROWS_AS(out.outputAtom(1u, "a("), std::invalid_argument); }
            SECTION("missing arg") { REQUIRE_THROWS_AS(out.outputAtom(1u, "a(1,"), std::invalid_argument); }
            SECTION("missing arg on close") { REQUIRE_THROWS_AS(out.outputAtom(1u, "a(1,)"), std::invalid_argument); }
            SECTION("empty arg") { REQUIRE_THROWS_AS(out.outputAtom(1u, "a(1,,2)"), std::invalid_argument); }
            SECTION("unmatched close") { REQUIRE_THROWS_AS(out.outputAtom(1u, "a(x()"), std::invalid_argument); }
            SECTION("empty arg 2") { REQUIRE_THROWS_AS(out.outputAtom(1u, "b(,)"), std::invalid_argument); }
        }
        SECTION("incremental") {
            out.initProgram(true);
            out.beginStep();
            rb.start(HeadType::choice).addHead(1).addHead(2).end(&out);
            out.outputAtom(2, "f");
            out.endStep();

            out.beginStep();
            out.outputAtom(2, "foo");
            rb.start().addHead(3).addGoal(1).addGoal(2).end(&out);
            out.outputTerm(0, "f");
            out.outputTerm(1, "nX2");
            out.output(0, Vec<Lit_t>{3});
            out.output(1, Vec<Lit_t>{2});

            REQUIRE(end() == "% #program base.\n"
                             "{x_1;f}.\n"
                             "#show f/0.\n"
                             "% #program step(1).\n"
                             "foo :- f.\n"
                             "x_3 :- x_1, f.\n"
                             "#show f : x_3.\n"
                             "#show nX2 : f.\n"
                             "#show foo/0.\n");
        }
    }
}

TEST_CASE("Text writer writes theory", "[text]") {
    std::stringstream output;
    AspifTextOutput   out(output);
    using namespace std::literals;
    out.initProgram(false);
    out.beginStep();
    SECTION("parens") {
        using namespace std::literals;
        CHECK(parens(TupleType::paren) == "()"sv);
        CHECK(parens(TupleType::brace) == "{}"sv);
        CHECK(parens(TupleType::bracket) == "[]"sv);
    }

    SECTION("write empty atom") {
        out.theoryAtom(0, 0, {});
        out.theoryTerm(0, "t");
        out.endStep();
        REQUIRE(output.str() == "&t{}.\n");
    }
    SECTION("write operators") {
        out.theoryTerm(0, "t");
        out.theoryTerm(1, "x");
        out.theoryTerm(2, "y");
        out.theoryTerm(3, "^~\\?.");
        std::vector<Id_t> ids;
        out.theoryTerm(4, 3, (ids = {1, 2}));
        out.theoryElement(0, (ids = {4}), {});
        out.theoryAtom(0, 0, (ids = {0}));
        out.endStep();
        REQUIRE(output.str() == "&t{x ^~\\?. y}.\n");
    }
    SECTION("write compound") {
        out.theoryTerm(0, "t");
        out.theoryTerm(1, "x");
        out.theoryTerm(2, "y");
        auto comp = 0;
        auto sep  = std::pair<std::string, std::string>{};
        auto ids  = std::vector<Id_t>{};
        REQUIRE_THROWS(out.theoryTerm(3, -4, (ids = {1, 2})));
        SECTION("tuple") {
            comp = static_cast<int>(TupleType::paren);
            sep  = std::make_pair("("s, ")"s);
        }
        SECTION("set") {
            comp = static_cast<int>(TupleType::brace);
            sep  = std::make_pair("{"s, "}"s);
        }
        SECTION("list") {
            comp = static_cast<int>(TupleType::bracket);
            sep  = std::make_pair("["s, "]"s);
        }
        SECTION("func") {
            out.theoryTerm(4, "f");
            comp = 4;
            sep  = std::make_pair("f("s, ")"s);
        }
        out.theoryTerm(3, comp, (ids = {1, 2}));
        out.theoryElement(0, (ids = {3}), {});
        out.theoryAtom(0, 0, (ids = {0}));
        out.endStep();
        REQUIRE(output.str() == std::string("&t{").append(sep.first).append("x, y").append(sep.second).append("}.\n"));
    }
    SECTION("write complex atom") {
        out.theoryTerm(1, 200);
        out.theoryTerm(3, 400);
        out.theoryTerm(6, 1);
        out.theoryTerm(11, 2);
        out.theoryTerm(0, "diff");
        out.theoryTerm(2, "<=");
        out.theoryTerm(4, "-");
        out.theoryTerm(5, "end");
        out.theoryTerm(8, "start");
        std::vector<Id_t> ids;
        out.theoryTerm(7, 5, (ids = {6}));
        out.theoryTerm(9, 8, (ids = {6}));
        out.theoryTerm(10, 4, (ids = {7, 9}));
        out.theoryTerm(12, 5, (ids = {11}));
        out.theoryTerm(13, 8, (ids = {11}));
        out.theoryTerm(14, 4, (ids = {12, 13}));

        out.theoryElement(0, (ids = {10}), {});
        out.theoryElement(1, (ids = {14}), {});

        out.theoryAtom(0, 0, (ids = {0}), 2, 1);
        out.theoryAtom(0, 0, (ids = {1}), 2, 3);
        out.endStep();
        REQUIRE(output.str() == "&diff{end(1) - start(1)} <= 200.\n"
                                "&diff{end(2) - start(2)} <= 400.\n");
    }
    SECTION("Use theory atom in rule") {
        Atom_t head = 2;
        Lit_t  body = 1;
        out.rule(HeadType::disjunctive, toSpan(head), toSpan(body));
        out.theoryTerm(0, "atom");
        out.theoryTerm(1, "x");
        out.theoryTerm(2, "y");
        std::vector<Id_t> ids;
        out.theoryElement(0, (ids = {1, 2}), {});
        out.theoryElement(1, (ids = {2}), {});
        out.theoryAtom(1, 0, (ids = {0, 1}));
        out.endStep();
        REQUIRE(output.str() == "x_2 :- &atom{x, y; y}.\n#show.\n");
    }
    SECTION("Fail on duplicate theory atom") {
        out.theoryTerm(0, "t");
        out.theoryTerm(1, "x");
        out.theoryAtom(1, 0, {});
        out.theoryAtom(1, 1, {});
        REQUIRE_THROWS_AS(out.endStep(), std::logic_error);
    }
    SECTION("Theory element with condition") {
        std::vector<Atom_t> head;
        std::vector<Id_t>   ids;
        std::vector<Lit_t>  body;
        out.rule(HeadType::choice, (head = {1, 2}), {});
        out.rule(HeadType::disjunctive, (head = {4}), (body = {3}));
        out.theoryTerm(0, "atom");
        out.theoryTerm(1, "elem");
        out.theoryTerm(2, "p");
        out.theoryElement(0, (ids = {1}), (body = {1, -2}));
        out.theoryElement(1, (ids = {2}), (body = {1}));
        out.theoryAtom(3, 0, (ids = {0, 1}));
        SECTION("default") {
            out.endStep();
            REQUIRE(output.str() == "{x_1;x_2}.\n"
                                    "x_4 :- &atom{elem : x_1, not x_2; p : x_1}.\n#show.\n");
        }
    }
    SECTION("write complex atom incrementally") {
        out.initProgram(true);
        out.beginStep();
        out.theoryTerm(1, 200);
        out.theoryTerm(6, 1);
        out.theoryTerm(11, 2);
        out.theoryTerm(0, "diff");
        out.theoryTerm(2, "<=");
        out.theoryTerm(4, "-");
        out.theoryTerm(5, "end");
        out.theoryTerm(8, "start");
        std::vector<Id_t> ids;
        out.theoryTerm(7, 5, (ids = {6}));
        out.theoryTerm(9, 8, (ids = {6}));
        out.theoryTerm(10, 4, (ids = {7, 9}));

        out.theoryElement(0, (ids = {10}), {});
        out.theoryAtom(0, 0, (ids = {0}), 2, 1);
        out.endStep();
        REQUIRE(output.str() == "% #program base.\n"
                                "&diff{end(1) - start(1)} <= 200.\n");
        output.str("");
        out.beginStep();
        out.theoryTerm(1, 600);
        out.theoryTerm(12, 5, (ids = {11}));
        out.theoryTerm(13, 8, (ids = {11}));
        out.theoryTerm(14, 4, (ids = {12, 13}));
        out.theoryElement(0, (ids = {14}), {});
        out.theoryAtom(0, 0, (ids = {0}), 2, 1);
        out.endStep();
        REQUIRE(output.str() == "% #program step(1).\n"
                                "&diff{end(2) - start(2)} <= 600.\n");
    }
    SECTION("invalid increment") {
        out.initProgram(true);
        out.beginStep();
        out.theoryTerm(0, "t");
        std::vector<Atom_t> head;
        std::vector<Lit_t>  body;
        out.rule(HeadType::choice, (head = {1, 2}), {});
        out.outputAtom(1u, "a");
        out.outputAtom(2u, "b");
        out.endStep();
        REQUIRE(output.str() == "% #program base.\n"
                                "{a;b}.\n");

        output.str("");
        out.beginStep();
        out.theoryAtom(2, 0, {});
        out.rule(HeadType::choice, (head = {4}), (body = {2}));
        REQUIRE_THROWS_AS(out.endStep(), std::logic_error);
    }
}
} // namespace Potassco::Test::Text
