#include <catch2/catch.hpp>
#include <loom/build_cache.hpp>
#include <loom/sha256.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

using namespace loom;
namespace fs = std::filesystem;

// RAII temp directory for benchmark databases
struct BenchTempDir {
    fs::path path;

    BenchTempDir() {
        path = fs::temp_directory_path() / ("loom_bench_cache_" + std::to_string(
            std::hash<std::string>{}(std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))));
        fs::create_directories(path);
    }

    ~BenchTempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// Build a realistic ParseResult for a module
static ParseResult make_parse_result(int module_index) {
    ParseResult pr;
    DesignUnit du;
    du.kind = DesignUnitKind::Module;
    du.name = "mod_" + std::to_string(module_index);
    du.start_line = 1;
    du.end_line = 50;

    // Typical module: 4 ports, 2 params, 2 instantiations, 1 always block
    du.ports.push_back({"clk", PortDirection::Input, "wire", {du.name, 3, 4}});
    du.ports.push_back({"rst", PortDirection::Input, "wire", {du.name, 4, 4}});
    du.ports.push_back({"data_in", PortDirection::Input, "logic [7:0]", {du.name, 5, 4}});
    du.ports.push_back({"data_out", PortDirection::Output, "logic [7:0]", {du.name, 6, 4}});

    du.params.push_back({"WIDTH", "8", false, {du.name, 2, 14}});
    du.params.push_back({"DEPTH", "16", false, {du.name, 2, 28}});

    if (module_index > 0) {
        du.instantiations.push_back({
            "mod_" + std::to_string(module_index - 1),
            "u_sub", true, {du.name, 20, 4}
        });
    }

    AlwaysBlock ab;
    ab.kind = AlwaysKind::Ff;
    ab.assignments.push_back({false, "data_out", {du.name, 30, 8}});
    ab.assignments.push_back({false, "wr_ptr", {du.name, 31, 8}});
    du.always_blocks.push_back(ab);

    du.signals.push_back({"wr_ptr", "reg [3:0]", {du.name, 10, 4}});
    du.signals.push_back({"rd_ptr", "reg [3:0]", {du.name, 11, 4}});

    pr.units.push_back(std::move(du));
    return pr;
}

// Generate a deterministic content hash for file N
static std::string file_hash(int n) {
    return SHA256::hash_hex("file_content_" + std::to_string(n));
}

// ---------------------------------------------------------------------------
// Benchmark 1: Stat lookup latency
// ---------------------------------------------------------------------------

TEST_CASE("build cache perf: stat lookup < 0.1ms", "[build_cache][bench]") {
    BenchTempDir td;
    BuildCache cache;
    auto status = cache.open((td.path / "bench.db").string());
    REQUIRE(status.is_ok());

    // Populate 1000 stat entries
    for (int i = 0; i < 1000; ++i) {
        FileStatEntry entry;
        entry.path = "/project/src/mod_" + std::to_string(i) + ".sv";
        entry.inode = 100000 + i;
        entry.mtime_sec = 1700000000 + i;
        entry.mtime_nsec = 0;
        entry.size = 2000 + i;
        entry.content_hash = file_hash(i);
        auto s = cache.update_stat(entry);
        REQUIRE(s.is_ok());
    }

    // Warm up
    auto warmup = cache.lookup_stat("/project/src/mod_500.sv");
    REQUIRE(warmup.is_ok());

    // Measure: 1000 random lookups
    const int N = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        // Lookup in a scattered pattern to avoid sequential cache effects
        int idx = (i * 7 + 3) % 1000;
        auto r = cache.lookup_stat("/project/src/mod_" + std::to_string(idx) + ".sv");
        REQUIRE(r.is_ok());
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double avg_us = total_us / N;
    double avg_ms = avg_us / 1000.0;

    WARN("Stat lookups: " << N);
    WARN("Total: " << total_us / 1000.0 << " ms");
    WARN("Average: " << avg_us << " us (" << avg_ms << " ms)");

    // Target: < 0.1ms per lookup
    REQUIRE(avg_ms < 0.1);
}

// ---------------------------------------------------------------------------
// Benchmark 2: Parse result lookup latency
// ---------------------------------------------------------------------------

TEST_CASE("build cache perf: parse lookup < 0.5ms", "[build_cache][bench]") {
    BenchTempDir td;
    BuildCache cache;
    auto status = cache.open((td.path / "bench.db").string());
    REQUIRE(status.is_ok());

    // Populate 1000 parse results (realistic module data)
    for (int i = 0; i < 1000; ++i) {
        auto pr = make_parse_result(i);
        auto s = cache.store_parse(file_hash(i), pr);
        REQUIRE(s.is_ok());
    }

    // Warm up
    auto warmup = cache.lookup_parse(file_hash(500));
    REQUIRE(warmup.is_ok());

    // Measure: 1000 random lookups
    const int N = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        int idx = (i * 7 + 3) % 1000;
        auto r = cache.lookup_parse(file_hash(idx));
        REQUIRE(r.is_ok());
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double avg_us = total_us / N;
    double avg_ms = avg_us / 1000.0;

    WARN("Parse lookups: " << N);
    WARN("Total: " << total_us / 1000.0 << " ms");
    WARN("Average: " << avg_us << " us (" << avg_ms << " ms)");

    // Target: < 0.5ms per lookup
    REQUIRE(avg_ms < 0.5);
}

// ---------------------------------------------------------------------------
// Benchmark 3: Full incremental check (1000 files, 0 changed)
// ---------------------------------------------------------------------------

TEST_CASE("build cache perf: 1000-file incremental check < 200ms", "[build_cache][bench]") {
    BenchTempDir td;

    // Create 1000 real files on disk so stat() works
    fs::path project_dir = td.path / "project";
    fs::create_directories(project_dir / "src");

    for (int i = 0; i < 1000; ++i) {
        std::string filename = "mod_" + std::to_string(i) + ".sv";
        fs::path filepath = project_dir / "src" / filename;
        std::ofstream f(filepath);
        f << "module mod_" << i << " #(\n"
          << "    parameter WIDTH = 8\n"
          << ")(\n"
          << "    input  wire clk,\n"
          << "    input  wire rst,\n"
          << "    input  wire [WIDTH-1:0] data_in,\n"
          << "    output reg  [WIDTH-1:0] data_out\n"
          << ");\n"
          << "    always @(posedge clk) data_out <= data_in;\n"
          << "endmodule\n";
    }

    // Build the cache: populate stat + parse entries for all 1000 files
    BuildCache cache;
    auto status = cache.open((td.path / "bench.db").string());
    REQUIRE(status.is_ok());

    std::vector<std::string> file_paths;
    for (int i = 0; i < 1000; ++i) {
        std::string filename = "mod_" + std::to_string(i) + ".sv";
        fs::path filepath = project_dir / "src" / filename;
        file_paths.push_back(filepath.string());

        // Stat the file
        struct stat st;
        ::stat(filepath.string().c_str(), &st);

        FileStatEntry fse;
        fse.path = filepath.string();
        fse.inode = st.st_ino;
        fse.mtime_sec = st.st_mtime;
        fse.mtime_nsec = 0;
        fse.size = st.st_size;
        fse.content_hash = file_hash(i);
        auto s1 = cache.update_stat(fse);
        REQUIRE(s1.is_ok());

        auto pr = make_parse_result(i);
        auto s2 = cache.store_parse(file_hash(i), pr);
        REQUIRE(s2.is_ok());
    }

    // Now simulate the full incremental check:
    // For each file: stat lookup -> check match -> parse lookup
    // This is what `loom build` does when nothing has changed.

    // Warm up
    {
        auto r = cache.lookup_stat(file_paths[0]);
        REQUIRE(r.is_ok());
        auto p = cache.lookup_parse(r.value().content_hash);
        REQUIRE(p.is_ok());
    }

    int hits = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i) {
        // Step 1: Stat lookup
        auto stat_result = cache.lookup_stat(file_paths[i]);
        if (stat_result.is_err()) continue;

        auto& cached_stat = stat_result.value();

        // Step 2: Compare stat against current file
        struct stat current_st;
        if (::stat(file_paths[i].c_str(), &current_st) != 0) continue;

        bool stat_matches = (cached_stat.inode == static_cast<uint64_t>(current_st.st_ino) &&
                            cached_stat.mtime_sec == current_st.st_mtime &&
                            cached_stat.size == current_st.st_size);

        if (!stat_matches) continue;

        // Step 3: Parse lookup using cached content hash
        auto parse_result = cache.lookup_parse(cached_stat.content_hash);
        if (parse_result.is_err()) continue;

        ++hits;
    }

    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    WARN("Files checked: 1000");
    WARN("Cache hits: " << hits);
    WARN("Total time: " << total_ms << " ms");
    WARN("Per-file: " << total_ms / 1000.0 << " ms");

    // All 1000 should hit
    REQUIRE(hits == 1000);

    // Target: < 200ms for full incremental check
    REQUIRE(total_ms < 200.0);
}
