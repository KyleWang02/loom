#include <catch2/catch.hpp>
#include <loom/graph.hpp>
#include <chrono>

using namespace loom;

TEST_CASE("graph perf: topo sort 10K nodes under 50ms", "[graph][bench]") {
    // Build a long chain: 0 -> 1 -> 2 -> ... -> 9999
    Graph<int> g;
    for (int i = 0; i < 10000; ++i) {
        g.add_node(i);
    }
    for (int i = 0; i < 9999; ++i) {
        g.add_edge(i, i + 1);
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto r = g.topological_sort();
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 10000);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    INFO("Topo sort 10K chain: " << ms << " ms");
    REQUIRE(ms < 50);
}

TEST_CASE("graph perf: topo sort 10K nodes wide DAG under 50ms", "[graph][bench]") {
    // Fan-out DAG: root -> 100 middle nodes, each middle -> 99 leaf nodes
    // Total: 1 + 100 + 9900 = 10001 nodes
    Graph<int> g;
    auto root = g.add_node(0);
    std::vector<size_t> mid;
    for (int i = 0; i < 100; ++i) {
        auto m = g.add_node(i + 1);
        g.add_edge(root, m);
        mid.push_back(m);
    }
    int leaf_id = 101;
    for (auto m : mid) {
        for (int j = 0; j < 99; ++j) {
            auto l = g.add_node(leaf_id++);
            g.add_edge(m, l);
        }
    }

    REQUIRE(g.node_count() == 10001);

    auto start = std::chrono::high_resolution_clock::now();
    auto r = g.topological_sort();
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(r.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    INFO("Topo sort 10K wide DAG: " << ms << " ms");
    REQUIRE(ms < 50);
}

TEST_CASE("graph perf: cycle detection 10K nodes under 50ms", "[graph][bench]") {
    // Chain with cycle at the end: 0->1->...->9998->9999->0
    Graph<int> g;
    for (int i = 0; i < 10000; ++i) {
        g.add_node(i);
    }
    for (int i = 0; i < 9999; ++i) {
        g.add_edge(i, i + 1);
    }
    g.add_edge(9999, 0); // close the cycle

    auto start = std::chrono::high_resolution_clock::now();
    bool cycle = g.has_cycle();
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(cycle);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    INFO("Cycle detection 10K: " << ms << " ms");
    REQUIRE(ms < 50);
}

TEST_CASE("graph perf: GraphMap topo sort 10K under 50ms", "[graph][bench]") {
    GraphMap<> gm;
    for (int i = 0; i < 10000; ++i) {
        gm.add_node("node_" + std::to_string(i));
    }
    for (int i = 0; i < 9999; ++i) {
        gm.add_edge("node_" + std::to_string(i),
                     "node_" + std::to_string(i + 1));
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto r = gm.topological_sort();
    auto end = std::chrono::high_resolution_clock::now();

    REQUIRE(r.is_ok());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    INFO("GraphMap topo sort 10K: " << ms << " ms");
    REQUIRE(ms < 50);
}
