//
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
#include <potassco/graph.h>

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace Potassco::Test::Graph {
using Scc    = std::vector<uint32_t>;
using SccVec = std::vector<Scc>;
namespace {
struct Fixture {
    std::string toString(const SccVec& sccs) {
        std::ostringstream out;
        out << "[";
        std::string_view sccVecSeparator;
        for (const auto& scc : sccs) {
            out << sccVecSeparator << "[";
            std::string_view sccSeparator;
            for (auto id : scc) {
                out << sccSeparator << static_cast<char>('a' + id);
                sccSeparator = ",";
            }
            out << "]";
            sccVecSeparator = ",";
        }
        out << "]";
        return out.str();
    }
    [[nodiscard]] auto computeSccs(bool skipTriv = false) -> SccVec {
        SccVec res;
        g.computeSccs([&](uint32_t, std::span<uint32_t> scc) { res.emplace_back(scc.begin(), scc.end()); }, skipTriv);
        return res;
    }
    [[nodiscard]] auto computeNonTrivialSccs() -> SccVec { return computeSccs(true); }

    Potassco::Graph<uint32_t> g;
};
} // end unnamed namespace

TEST_CASE_METHOD(Fixture, "Graph empty", "[reify][graph]") { REQUIRE(computeSccs().empty()); }

TEST_CASE_METHOD(Fixture, "Graph single node", "[reify][graph]") {
    g.addNode(0);
    auto sccs = computeSccs();
    REQUIRE(sccs.size() == 1);
    REQUIRE(toString(sccs) == "[[a]]");
}

TEST_CASE_METHOD(Fixture, "Graph acyclic", "[reify][graph]") {
    auto idA = g.addNode(0);
    auto idB = g.addNode(1);
    auto idC = g.addNode(2);

    g.addEdge(idA, idB);
    g.addEdge(idB, idC);

    auto sccs = computeSccs();
    REQUIRE(sccs.size() == 3);
    REQUIRE(toString(sccs) == "[[c],[b],[a]]");
    REQUIRE(computeNonTrivialSccs().empty());
}

TEST_CASE_METHOD(Fixture, "Graph single cycle", "[reify][graph]") {
    auto idA = g.addNode(0);
    auto idB = g.addNode(1);
    auto idC = g.addNode(2);
    g.addNode(3);

    g.addEdge(idA, idB);
    g.addEdge(idB, idC);
    g.addEdge(idC, idA);

    auto sccs = computeSccs();
    REQUIRE(sccs.size() == 2);
    REQUIRE(toString(sccs) == "[[c,b,a],[d]]");
}

TEST_CASE_METHOD(Fixture, "Graph multiple cycles", "[reify][graph]") {
    auto idA = g.addNode(0);
    auto idB = g.addNode(1);
    auto idC = g.addNode(2);
    auto idD = g.addNode(3);
    auto idE = g.addNode(4);
    auto idF = g.addNode(5);
    auto idG = g.addNode(6);
    auto idH = g.addNode(7);
    auto idI = g.addNode(8);

    g.addEdge(idA, idG);
    g.addEdge(idB, idE);
    g.addEdge(idB, idH);
    g.addEdge(idC, idI);
    g.addEdge(idC, idH);
    g.addEdge(idD, idF);
    g.addEdge(idE, idA);
    g.addEdge(idF, idB);
    g.addEdge(idF, idC);
    g.addEdge(idG, idD);

    REQUIRE(toString(computeSccs()) == "[[h],[i],[c],[e,b,f,d,g,a]]");
}

TEST_CASE_METHOD(Fixture, "Graph preserved", "[reify][graph]") {
    auto idA = g.addNode(0);
    auto idB = g.addNode(1);
    auto idC = g.addNode(2);
    auto idD = g.addNode(3);
    auto idE = g.addNode(4);
    auto idF = g.addNode(5);
    auto idG = g.addNode(6);
    auto idH = g.addNode(7);
    auto idI = g.addNode(8);

    g.addEdge(idA, idB);
    g.addEdge(idB, idC);
    g.addEdge(idC, idH);
    g.addEdge(idC, idD);
    g.addEdge(idD, idE);
    g.addEdge(idE, idF);
    g.addEdge(idE, idB);
    g.addEdge(idE, idC);
    g.addEdge(idF, idG);
    g.addEdge(idG, idF);
    g.addEdge(idH, idI);
    g.addEdge(idI, idH);

    const auto expected = "[[i,h],[g,f],[e,d,c,b],[a]]";
    REQUIRE(toString(computeSccs()) == expected);
    REQUIRE(toString(computeSccs()) == expected);
    REQUIRE(toString(computeSccs()) == expected);
}

} // namespace Potassco::Test::Graph
