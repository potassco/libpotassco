//
// Copyright (c) 2017 - 2025, Roland Kaminski
// Copyright (c) 2025 - present, Francois Laferriere
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
#include <potassco/aspif_text.h>
#include <potassco/reify.h>

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace Potassco::Test::Reify {

namespace {
struct Fixture {
    bool readText(bool scc = false, bool step = false) {
        Reifier        prg(out, {scc, step});
        AspifTextInput parser(&prg);
        return readProgram(in, parser) == 0;
    }

    bool readAspif(bool scc = false, bool step = false) {
        Reifier    prg(out, {scc, step});
        AspifInput parser(prg);
        return readProgram(in, parser) == 0;
    }

    std::stringstream in;
    std::stringstream out;
};
} // end unnamed namespace

TEST_CASE_METHOD(Fixture, "Reify empty", "[reify]") {
    REQUIRE(readText());
    REQUIRE(out.str() == "");
}

TEST_CASE_METHOD(Fixture, "Reify incremental", "[reify]") {
    in << "#incremental.";
    REQUIRE(readText());
    REQUIRE(out.view() == "tag(incremental).\n");
}

TEST_CASE_METHOD(Fixture, "Reify normal", "[reify]") {
    in << "a:-b.";
    REQUIRE(readText());
    REQUIRE(
        out.str() ==
        "atom_tuple(0).\natom_tuple(0,1).\nliteral_tuple(0).\nliteral_tuple(0,2).\nrule(disjunction(0),normal(0)).\n");
}

TEST_CASE_METHOD(Fixture, "Reify step", "[reify]") {
    in << "#incremental.";
    in << "a:-b.";
    in << "#step.";
    in << "c:-b.";

    const auto expected = "tag(incremental).\n"
                          "atom_tuple(0,0).\n"
                          "atom_tuple(0,1,0).\n"
                          "literal_tuple(0,0).\n"
                          "literal_tuple(0,2,0).\n"
                          "rule(disjunction(0),normal(0),0).\n"
                          "atom_tuple(0,1).\n"
                          "atom_tuple(0,3,1).\n"
                          "literal_tuple(0,1).\n"
                          "literal_tuple(0,2,1).\n"
                          "rule(disjunction(0),normal(0),1).\n";

    REQUIRE(readText(false, true));
    REQUIRE(out.str() == expected);
}

TEST_CASE_METHOD(Fixture, "Reify cycle", "[reify]") {
    in << "a:-b.";
    in << "b:-a.";

    const auto expected = "atom_tuple(0).\n"
                          "atom_tuple(0,1).\n"
                          "literal_tuple(0).\n"
                          "literal_tuple(0,2).\n"
                          "rule(disjunction(0),normal(0)).\n"
                          "atom_tuple(1).\n"
                          "atom_tuple(1,2).\n"
                          "literal_tuple(1).\n"
                          "literal_tuple(1,1).\n"
                          "rule(disjunction(1),normal(1)).\n"
                          "scc(0,1).\n"
                          "scc(0,2).\n";

    REQUIRE(readText(true));
    REQUIRE(out.str() == expected);
}

TEST_CASE_METHOD(Fixture, "Reify choice", "[reify]") {
    in << "{a, b}.";
    REQUIRE(readText());
    REQUIRE(out.str() ==
            "atom_tuple(0).\natom_tuple(0,1).\natom_tuple(0,2).\nliteral_tuple(0).\nrule(choice(0),normal(0)).\n");
}

TEST_CASE_METHOD(Fixture, "Reify sum", "[reify]") {
    in << ":-1 {a, b}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "atom_tuple(0).\nweighted_literal_tuple(0).\nweighted_literal_tuple(0,1,1).\nweighted_literal_"
                         "tuple(0,2,1).\nrule(disjunction(0),sum(0,1)).\n");
}

TEST_CASE_METHOD(Fixture, "Reify minimize", "[reify]") {
    in << "#minimize {a=10, b=20}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "weighted_literal_tuple(0).\nweighted_literal_tuple(0,1,10).\nweighted_literal_tuple(0,2,20)."
                         "\nminimize(0,0).\n");
}

TEST_CASE_METHOD(Fixture, "Reify project", "[reify]") {
    in << "#project {a}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "project(1).\n");
}

TEST_CASE_METHOD(Fixture, "Reify output", "[reify]") {
    in << "#output a:b,c.";
    REQUIRE(readText());
    REQUIRE(out.str() ==
            "outputTerm(a,0).\nliteral_tuple(0).\nliteral_tuple(0,2).\nliteral_tuple(0,3).\noutput(0,0).\n");
}

TEST_CASE_METHOD(Fixture, "Reify external", "[reify]") {
    in << "#external a.";
    REQUIRE(readText());
    REQUIRE(out.str() == "external(1,false).\n");
}

TEST_CASE_METHOD(Fixture, "Reify assume", "[reify]") {
    in << "#assume {a}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "assume(1).\n");
}

TEST_CASE_METHOD(Fixture, "Reify heuristic", "[reify]") {
    in << "#heuristic a. [1, level]";
    in << "#heuristic b : c. [2@1, true]";
    REQUIRE(readText());
    REQUIRE(out.str() == "literal_tuple(0).\nheuristic(1,level,1,0,0).\nliteral_tuple(1).\nliteral_tuple(1,3)."
                         "\nheuristic(2,true,2,1,1).\n");
}

TEST_CASE_METHOD(Fixture, "Reify edge", "[reify]") {
    in << "#edge (1,2) : a.";
    in << "#edge (2,1).";
    REQUIRE(readText());
    REQUIRE(out.str() == "literal_tuple(0).\nliteral_tuple(0,1).\nedge(1,2,0).\nliteral_tuple(1).\nedge(2,1,1).\n");
}

TEST_CASE_METHOD(Fixture, "Reify sorted tuples", "[reify]") {
    in << " {a; b}.";
    in << " {b; a}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "atom_tuple(0).\natom_tuple(0,1).\natom_tuple(0,2).\nliteral_tuple(0).\nrule(choice(0),normal("
                         "0)).\nrule(choice(0),normal(0)).\n");
}

TEST_CASE_METHOD(Fixture, "Reify unique tuples", "[reify]") {
    in << " {a; b; a}.";
    in << " {a; b}.";
    REQUIRE(readText());
    REQUIRE(out.str() == "atom_tuple(0).\natom_tuple(0,1).\natom_tuple(0,2).\nliteral_tuple(0).\nrule(choice(0),normal("
                         "0)).\nrule(choice(0),normal(0)).\n");
}

TEST_CASE_METHOD(Fixture, "Reify theory terms", "[reify]") {
    in << "asp 1 0 0\n";
    in << "9 0 6 42\n";
    in << "9 1 0 6 banana\n";
    in << "9 2 14 4 2 12 13\n";
    in << "0";
    REQUIRE(readAspif());
    REQUIRE(out.str() == "theory_number(6,42).\ntheory_string(0,\"banana\").\ntheory_tuple(0).\ntheory_tuple(0,0,12)."
                         "\ntheory_tuple(0,1,13).\ntheory_function(14,4,0).\n");
}

TEST_CASE_METHOD(Fixture, "Reify theory atoms", "[reify]") {
    in << "asp 1 0 0\n";
    in << "9 4 0 1 10 0\n";
    in << "9 5 6 0 1 1\n";
    in << "9 6 6 0 1 1 2 3\n";
    in << "0";
    REQUIRE(readAspif());
    REQUIRE(out.str() ==
            "theory_tuple(0).\ntheory_tuple(0,0,10).\nliteral_tuple(0).\ntheory_element(0,0,0).\ntheory_element_tuple("
            "0).\ntheory_element_tuple(0,1).\ntheory_atom(6,0,0).\ntheory_atom(6,0,0,2,3).\n");
}

TEST_CASE_METHOD(Fixture, "Reify theory unique tuples", "[reify]") {
    in << "asp 1 0 0\n";
    in << "9 2 14 4 2 12 13\n";
    in << "9 2 37 9 2 12 13\n";
    in << "0";
    REQUIRE(readAspif());
    REQUIRE(out.str() == "theory_tuple(0).\ntheory_tuple(0,0,12).\ntheory_tuple(0,1,13).\ntheory_function(14,4,0)."
                         "\ntheory_function(37,9,0).\n");
}

TEST_CASE_METHOD(Fixture, "Reify theory quoting", "[reify]") {
    in << "asp 1 0 0\n";
    in << "9 1 0 6 hell\"o\n";
    in << "9 1 1 6 gre\\at\n";
    in << "9 1 2 6 worl\nd\n";
    in << "0";
    REQUIRE(readAspif());
    REQUIRE(out.str() ==
            "theory_string(0,\"hell\\\"o\").\ntheory_string(1,\"gre\\\\at\").\ntheory_string(2,\"worl\\nd\").\n");
}

} // namespace Potassco::Test::Reify
