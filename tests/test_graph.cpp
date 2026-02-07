#include <catch2/catch.hpp>
#include <loom/graph.hpp>

using namespace loom;

// ===== Basic Graph operations =====

TEST_CASE("graph add node and query", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    REQUIRE(g.node_count() == 2);
    REQUIRE(g.node(a) == "A");
    REQUIRE(g.node(b) == "B");
}

TEST_CASE("graph add edge and has_edge", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    REQUIRE_FALSE(g.has_edge(a, b));
    g.add_edge(a, b);
    REQUIRE(g.has_edge(a, b));
    REQUIRE_FALSE(g.has_edge(b, a));
}

TEST_CASE("graph successors and predecessors", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    g.add_edge(a, b);
    g.add_edge(a, c);
    REQUIRE(g.successors(a).size() == 2);
    REQUIRE(g.predecessors(b).size() == 1);
    REQUIRE(g.predecessors(b)[0] == a);
}

TEST_CASE("graph in_degree and out_degree", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    g.add_edge(a, b);
    g.add_edge(a, c);
    g.add_edge(b, c);
    REQUIRE(g.out_degree(a) == 2);
    REQUIRE(g.in_degree(a) == 0);
    REQUIRE(g.in_degree(c) == 2);
    REQUIRE(g.out_degree(c) == 0);
}

// ===== Topological sort =====

TEST_CASE("topo sort linear chain", "[graph]") {
    // A -> B -> C -> D
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    g.add_edge(a, b);
    g.add_edge(b, c);
    g.add_edge(c, d);

    auto r = g.topological_sort();
    REQUIRE(r.is_ok());
    auto& order = r.value();
    REQUIRE(order.size() == 4);
    // A must come before B, B before C, C before D
    size_t pos_a = 0, pos_b = 0, pos_c = 0, pos_d = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == a) pos_a = i;
        if (order[i] == b) pos_b = i;
        if (order[i] == c) pos_c = i;
        if (order[i] == d) pos_d = i;
    }
    REQUIRE(pos_a < pos_b);
    REQUIRE(pos_b < pos_c);
    REQUIRE(pos_c < pos_d);
}

TEST_CASE("topo sort diamond", "[graph]") {
    //   A
    //  / \
    // B   C
    //  \ /
    //   D
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    g.add_edge(a, b);
    g.add_edge(a, c);
    g.add_edge(b, d);
    g.add_edge(c, d);

    auto r = g.topological_sort();
    REQUIRE(r.is_ok());
    auto& order = r.value();
    REQUIRE(order.size() == 4);
    // A before B and C, B and C before D
    size_t pos_a = 0, pos_b = 0, pos_c = 0, pos_d = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == a) pos_a = i;
        if (order[i] == b) pos_b = i;
        if (order[i] == c) pos_c = i;
        if (order[i] == d) pos_d = i;
    }
    REQUIRE(pos_a < pos_b);
    REQUIRE(pos_a < pos_c);
    REQUIRE(pos_b < pos_d);
    REQUIRE(pos_c < pos_d);
}

TEST_CASE("topo sort single node", "[graph]") {
    Graph<std::string> g;
    g.add_node("A");
    auto r = g.topological_sort();
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 1);
}

TEST_CASE("topo sort empty graph", "[graph]") {
    Graph<std::string> g;
    auto r = g.topological_sort();
    REQUIRE(r.is_ok());
    REQUIRE(r.value().empty());
}

TEST_CASE("topo sort disconnected components", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    g.add_edge(a, b);
    g.add_edge(c, d);

    auto r = g.topological_sort();
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 4);
}

// ===== Cycle detection =====

TEST_CASE("cycle detection on DAG", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    g.add_edge(a, b);
    REQUIRE_FALSE(g.has_cycle());
}

TEST_CASE("cycle detection on self-loop", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    g.add_edge(a, a);
    REQUIRE(g.has_cycle());
}

TEST_CASE("cycle detection on two-node cycle", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    g.add_edge(a, b);
    g.add_edge(b, a);
    REQUIRE(g.has_cycle());
}

TEST_CASE("cycle detection on three-node cycle", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    g.add_edge(a, b);
    g.add_edge(b, c);
    g.add_edge(c, a);
    REQUIRE(g.has_cycle());
}

TEST_CASE("topo sort returns cycle error", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    g.add_edge(a, b);
    g.add_edge(b, a);
    auto r = g.topological_sort();
    REQUIRE(r.is_err());
    REQUIRE(r.error().code == LoomError::Cycle);
}

// ===== Minimal topological sort =====

TEST_CASE("topo sort from root", "[graph]") {
    // A -> B -> D
    // A -> C
    // E -> F (disconnected)
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    auto e = g.add_node("E");
    auto f = g.add_node("F");
    g.add_edge(a, b);
    g.add_edge(b, d);
    g.add_edge(a, c);
    g.add_edge(e, f);

    auto r = g.topological_sort_from(a);
    REQUIRE(r.is_ok());
    auto& order = r.value();
    // Should only contain A, B, C, D (not E, F)
    REQUIRE(order.size() == 4);
    std::unordered_set<size_t> ids(order.begin(), order.end());
    REQUIRE(ids.count(a));
    REQUIRE(ids.count(b));
    REQUIRE(ids.count(c));
    REQUIRE(ids.count(d));
    REQUIRE_FALSE(ids.count(e));
    REQUIRE_FALSE(ids.count(f));
}

// ===== DFS traversal =====

TEST_CASE("dfs visits all reachable nodes", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");  // disconnected
    g.add_edge(a, b);
    g.add_edge(b, c);

    std::vector<size_t> visited;
    g.dfs(a, [&](size_t id) { visited.push_back(id); });

    REQUIRE(visited.size() == 3);
    // A should be first (pre-order DFS)
    REQUIRE(visited[0] == a);
    // D should not be visited
    bool found_d = false;
    for (auto id : visited) {
        if (id == d) found_d = true;
    }
    REQUIRE_FALSE(found_d);
}

TEST_CASE("dfs handles diamond without duplicates", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    g.add_edge(a, b);
    g.add_edge(a, c);
    g.add_edge(b, d);
    g.add_edge(c, d);

    std::vector<size_t> visited;
    g.dfs(a, [&](size_t id) { visited.push_back(id); });

    // D should appear exactly once despite two paths
    REQUIRE(visited.size() == 4);
}

// ===== Tree display =====

TEST_CASE("tree display simple", "[graph]") {
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    g.add_edge(a, b);
    g.add_edge(a, c);

    auto display = g.tree_display(a,
        [](const std::string& s) { return s; });

    REQUIRE(display.find("A") != std::string::npos);
    REQUIRE(display.find("B") != std::string::npos);
    REQUIRE(display.find("C") != std::string::npos);
}

TEST_CASE("tree display marks repeated nodes", "[graph]") {
    // Diamond: A -> B -> D, A -> C -> D
    // D should appear with (*) on second visit
    Graph<std::string> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    auto c = g.add_node("C");
    auto d = g.add_node("D");
    g.add_edge(a, b);
    g.add_edge(a, c);
    g.add_edge(b, d);
    g.add_edge(c, d);

    auto display = g.tree_display(a,
        [](const std::string& s) { return s; });

    REQUIRE(display.find("(*)") != std::string::npos);
}

// ===== GraphMap (string-keyed wrapper) =====

TEST_CASE("graphmap add and lookup", "[graph]") {
    GraphMap<> gm;
    gm.add_node("uart");
    gm.add_node("spi");
    REQUIRE(gm.has_node("uart"));
    REQUIRE(gm.has_node("spi"));
    REQUIRE_FALSE(gm.has_node("i2c"));
    REQUIRE(gm.node_count() == 2);
}

TEST_CASE("graphmap duplicate add returns same id", "[graph]") {
    GraphMap<> gm;
    auto id1 = gm.add_node("uart");
    auto id2 = gm.add_node("uart");
    REQUIRE(id1 == id2);
    REQUIRE(gm.node_count() == 1);
}

TEST_CASE("graphmap add_edge auto-creates nodes", "[graph]") {
    GraphMap<> gm;
    gm.add_edge("uart", "common_cells");
    REQUIRE(gm.has_node("uart"));
    REQUIRE(gm.has_node("common_cells"));
    REQUIRE(gm.node_count() == 2);
}

TEST_CASE("graphmap topological sort", "[graph]") {
    GraphMap<> gm;
    gm.add_edge("soc", "uart");
    gm.add_edge("soc", "spi");
    gm.add_edge("uart", "common");
    gm.add_edge("spi", "common");

    auto r = gm.topological_sort();
    REQUIRE(r.is_ok());
    auto& order = r.value();
    REQUIRE(order.size() == 4);

    // soc must come before uart and spi, both before common
    auto pos = [&](const std::string& name) -> size_t {
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == name) return i;
        }
        return order.size();
    };
    REQUIRE(pos("soc") < pos("uart"));
    REQUIRE(pos("soc") < pos("spi"));
    REQUIRE(pos("uart") < pos("common"));
    REQUIRE(pos("spi") < pos("common"));
}

TEST_CASE("graphmap cycle detection", "[graph]") {
    GraphMap<> gm;
    gm.add_edge("A", "B");
    gm.add_edge("B", "A");
    REQUIRE(gm.has_cycle());
}

TEST_CASE("graphmap tree display", "[graph]") {
    GraphMap<> gm;
    gm.add_edge("soc", "uart");
    gm.add_edge("soc", "spi");

    auto display = gm.tree_display("soc");
    REQUIRE(display.find("soc") != std::string::npos);
    REQUIRE(display.find("uart") != std::string::npos);
    REQUIRE(display.find("spi") != std::string::npos);
}

TEST_CASE("graphmap tree display nonexistent root", "[graph]") {
    GraphMap<> gm;
    gm.add_node("A");
    REQUIRE(gm.tree_display("Z") == "");
}

// ===== Edge data =====

TEST_CASE("graph with edge data", "[graph]") {
    Graph<std::string, int> g;
    auto a = g.add_node("A");
    auto b = g.add_node("B");
    g.add_edge(a, b, 42);
    REQUIRE(g.successors(a)[0].data == 42);
}
