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
#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace Potassco {
/////////////////////////////////////////////////////////////////////////////////////////
// Graph template
/////////////////////////////////////////////////////////////////////////////////////////
template <std::integral DataType = uintptr_t, std::unsigned_integral IdType = uint32_t>
class Graph {
public:
    Graph() = default;
    auto addNode(DataType data) -> IdType {
        auto id = static_cast<IdType>(nodes_.size());
        nodes_.emplace_back(data, open_);
        return id;
    }
    auto addEdge(IdType from, IdType to) -> void { nodes_.at(from).edges.push_back(to); }
    void clear() { nodes_.clear(); }

    using Scc    = std::vector<DataType>;
    using SccVec = std::vector<Scc>;
    /*!
     * \brief Compute strongly connected components (SCCs) using Tarjan's algorithm.
     *
     * \param skipTrivial If true, SCCs of size 1 are omitted from the returned vector.
     *
     * \note This function modifies the internal state of the graph (node visit markers and traversal offsets).
     *       Node IDs and edges remain unchanged.
     */
    auto computeSccs(bool skipTrivial = false) -> SccVec {
        using IdVec = std::vector<IdType>;
        SccVec     sccs;
        IdVec      stack;
        IdVec      trail;
        const auto open   = open_;      // open state, i.e. node not yet seen
        const auto closed = 1u - open_; // closed state, i.e. node has its scc determined
        for (auto xId = IdType(0); auto& x : nodes_) {
            if (x.min == open) {
                auto index = IdType(1);
                auto push  = [&](IdType nId, Node& n) {
                    n.min = ++index;
                    n.off = 0u;
                    stack.emplace_back(nId);
                    trail.emplace_back(nId);
                };
                push(xId, x);

                while (not stack.empty()) {
                    auto  yId      = stack.back();
                    auto& y        = nodes_[yId];
                    auto  finished = true;
                    for (auto zId : std::span(y.edges).subspan(y.off)) {
                        ++y.off;
                        if (auto& z = nodes_.at(zId); z.min == open) {
                            push(zId, z);
                            finished = false;
                            break;
                        }
                    }
                    if (finished) {
                        stack.pop_back();
                        bool root = true;
                        for (auto zId : y.edges) {
                            if (auto& z = nodes_[zId]; z.min != closed && z.min < y.min) {
                                assert(z.min != open && "stack invariant broken");
                                root  = false;
                                y.min = z.min;
                            }
                        }

                        if (root) {
                            Scc  scc;
                            auto nId = xId;
                            do {
                                nId = trail.back();
                                trail.pop_back();
                                auto& n = nodes_[nId];
                                n.min   = closed;
                                scc.emplace_back(n.data);
                            } while (nId != yId);

                            if (not skipTrivial || scc.size() > 1) {
                                sccs.push_back(std::move(scc));
                            }
                        }
                    }
                }
            }
            ++xId;
        }
        open_ = closed;
        return sccs;
    }
    //! Compute only SCCs of size > 1 (non-trivial).
    auto computeNonTrivialSccs() -> SccVec { return computeSccs(true); }

private:
    struct Node {
        Node(DataType d, IdType m) : data(d), min(m), off(0) {}
        std::vector<IdType> edges;
        DataType            data{0};
        IdType              min{0};
        IdType              off{0};
    };

    std::vector<Node> nodes_;
    IdType            open_ = 0; // current "unseen" state - either 0 or 1
};
} // namespace Potassco
