#pragma once

#include <loom/result.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <functional>
#include <sstream>

namespace loom {

// ---------------------------------------------------------------------------
// Graph<NodeData, EdgeData> — directed graph with adjacency list
// ---------------------------------------------------------------------------

template<typename NodeData, typename EdgeData = std::monostate>
class Graph {
public:
    using NodeId = size_t;

    struct Edge {
        NodeId from;
        NodeId to;
        EdgeData data;
    };

    NodeId add_node(NodeData data) {
        NodeId id = nodes_.size();
        nodes_.push_back(std::move(data));
        adj_.push_back({});
        radj_.push_back({});
        return id;
    }

    void add_edge(NodeId from, NodeId to, EdgeData data = {}) {
        adj_[from].push_back({from, to, std::move(data)});
        radj_[to].push_back(from);
    }

    bool has_edge(NodeId from, NodeId to) const {
        for (const auto& e : adj_[from]) {
            if (e.to == to) return true;
        }
        return false;
    }

    size_t node_count() const { return nodes_.size(); }

    const NodeData& node(NodeId id) const { return nodes_[id]; }
    NodeData& node(NodeId id) { return nodes_[id]; }

    const std::vector<Edge>& successors(NodeId id) const { return adj_[id]; }
    const std::vector<NodeId>& predecessors(NodeId id) const { return radj_[id]; }

    size_t in_degree(NodeId id) const { return radj_[id].size(); }
    size_t out_degree(NodeId id) const { return adj_[id].size(); }

    // Topological sort using Kahn's algorithm.
    // Returns sorted node IDs, or Cycle error if graph has cycles.
    Result<std::vector<NodeId>> topological_sort() const {
        size_t n = nodes_.size();
        std::vector<size_t> in_deg(n, 0);
        for (size_t i = 0; i < n; ++i) {
            in_deg[i] = radj_[i].size();
        }

        std::queue<NodeId> q;
        for (size_t i = 0; i < n; ++i) {
            if (in_deg[i] == 0) q.push(i);
        }

        std::vector<NodeId> order;
        order.reserve(n);
        while (!q.empty()) {
            NodeId u = q.front();
            q.pop();
            order.push_back(u);
            for (const auto& e : adj_[u]) {
                if (--in_deg[e.to] == 0) {
                    q.push(e.to);
                }
            }
        }

        if (order.size() != n) {
            return LoomError{LoomError::Cycle,
                "graph contains a cycle"};
        }
        return Result<std::vector<NodeId>>::ok(std::move(order));
    }

    // Minimal topological sort starting from a root node.
    // Only includes nodes reachable from root.
    Result<std::vector<NodeId>> topological_sort_from(NodeId root) const {
        // BFS to find reachable nodes
        std::unordered_set<NodeId> reachable;
        std::queue<NodeId> bfs;
        bfs.push(root);
        reachable.insert(root);
        while (!bfs.empty()) {
            NodeId u = bfs.front();
            bfs.pop();
            for (const auto& e : adj_[u]) {
                if (reachable.insert(e.to).second) {
                    bfs.push(e.to);
                }
            }
        }

        // Kahn's on the subgraph
        std::unordered_map<NodeId, size_t> in_deg;
        for (NodeId id : reachable) {
            in_deg[id] = 0;
        }
        for (NodeId id : reachable) {
            for (const auto& e : adj_[id]) {
                if (reachable.count(e.to)) {
                    ++in_deg[e.to];
                }
            }
        }

        std::queue<NodeId> q;
        for (auto& [id, deg] : in_deg) {
            if (deg == 0) q.push(id);
        }

        std::vector<NodeId> order;
        while (!q.empty()) {
            NodeId u = q.front();
            q.pop();
            order.push_back(u);
            for (const auto& e : adj_[u]) {
                if (reachable.count(e.to)) {
                    if (--in_deg[e.to] == 0) {
                        q.push(e.to);
                    }
                }
            }
        }

        if (order.size() != reachable.size()) {
            return LoomError{LoomError::Cycle,
                "graph contains a cycle in reachable subgraph"};
        }
        return Result<std::vector<NodeId>>::ok(std::move(order));
    }

    // Detect if the graph has a cycle.
    bool has_cycle() const {
        return topological_sort().is_err();
    }

    // DFS traversal from a starting node, calling visitor for each.
    void dfs(NodeId start,
             std::function<void(NodeId)> visitor) const {
        std::unordered_set<NodeId> visited;
        dfs_impl(start, visited, visitor);
    }

    // Tree display: format the dependency tree as a string.
    // to_string_fn converts NodeData to a display string.
    std::string tree_display(
        NodeId root,
        std::function<std::string(const NodeData&)> to_string_fn) const
    {
        std::ostringstream out;
        std::unordered_set<NodeId> visited;
        tree_display_impl(root, "", true, visited, to_string_fn, out);
        return out.str();
    }

private:
    std::vector<NodeData> nodes_;
    std::vector<std::vector<Edge>> adj_;
    std::vector<std::vector<NodeId>> radj_;

    void dfs_impl(NodeId u,
                  std::unordered_set<NodeId>& visited,
                  std::function<void(NodeId)>& visitor) const {
        if (!visited.insert(u).second) return;
        visitor(u);
        for (const auto& e : adj_[u]) {
            dfs_impl(e.to, visited, visitor);
        }
    }

    void tree_display_impl(
        NodeId u,
        const std::string& prefix,
        bool is_last,
        std::unordered_set<NodeId>& visited,
        std::function<std::string(const NodeData&)>& to_string_fn,
        std::ostringstream& out) const
    {
        out << prefix;
        if (!prefix.empty()) {
            out << (is_last ? "└── " : "├── ");
        }
        out << to_string_fn(nodes_[u]);

        if (!visited.insert(u).second) {
            out << " (*)\n";
            return;
        }
        out << "\n";

        auto& edges = adj_[u];
        for (size_t i = 0; i < edges.size(); ++i) {
            std::string child_prefix = prefix;
            if (!prefix.empty()) {
                child_prefix += (is_last ? "    " : "│   ");
            }
            tree_display_impl(edges[i].to, child_prefix,
                              i == edges.size() - 1,
                              visited, to_string_fn, out);
        }
    }
};

// ---------------------------------------------------------------------------
// GraphMap — string-keyed convenience wrapper
// ---------------------------------------------------------------------------

template<typename EdgeData = std::monostate>
class GraphMap {
public:
    using NodeId = typename Graph<std::string, EdgeData>::NodeId;

    NodeId add_node(const std::string& name) {
        auto it = name_to_id_.find(name);
        if (it != name_to_id_.end()) return it->second;
        NodeId id = graph_.add_node(name);
        name_to_id_[name] = id;
        return id;
    }

    // Returns true if the node exists
    bool has_node(const std::string& name) const {
        return name_to_id_.count(name) > 0;
    }

    NodeId node_id(const std::string& name) const {
        return name_to_id_.at(name);
    }

    void add_edge(const std::string& from, const std::string& to,
                  EdgeData data = {}) {
        NodeId f = add_node(from);
        NodeId t = add_node(to);
        graph_.add_edge(f, t, std::move(data));
    }

    Result<std::vector<std::string>> topological_sort() const {
        auto r = graph_.topological_sort();
        if (r.is_err()) return std::move(r).error();
        std::vector<std::string> names;
        for (auto id : r.value()) {
            names.push_back(graph_.node(id));
        }
        return Result<std::vector<std::string>>::ok(std::move(names));
    }

    bool has_cycle() const { return graph_.has_cycle(); }

    size_t node_count() const { return graph_.node_count(); }

    std::string tree_display(const std::string& root) const {
        auto it = name_to_id_.find(root);
        if (it == name_to_id_.end()) return "";
        return graph_.tree_display(it->second,
            [](const std::string& s) { return s; });
    }

    const Graph<std::string, EdgeData>& inner() const { return graph_; }

private:
    Graph<std::string, EdgeData> graph_;
    std::unordered_map<std::string, NodeId> name_to_id_;
};

} // namespace loom
