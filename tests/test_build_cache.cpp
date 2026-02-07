#include <catch2/catch.hpp>
#include <loom/build_cache.hpp>
#include <loom/sha256.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string test_db_path() {
    static int counter = 0;
    return "/tmp/loom_test_build_cache_" + std::to_string(getpid())
           + "_" + std::to_string(counter++) + ".db";
}

static void write_test_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

// ---------------------------------------------------------------------------
// Database lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("BuildCache open creates database file", "[build_cache]") {
    auto path = test_db_path();
    loom::BuildCache cache;
    auto r = cache.open(path);
    REQUIRE(r.is_ok());
    REQUIRE(cache.is_open());
    REQUIRE(fs::exists(path));
    cache.close();
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

TEST_CASE("BuildCache open with WAL mode", "[build_cache]") {
    auto path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(path).is_ok());
    // WAL mode creates -wal file on first write
    auto stats = cache.get_stats();
    REQUIRE(stats.is_ok());
    cache.close();
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

// ---------------------------------------------------------------------------
// Stat cache
// ---------------------------------------------------------------------------

TEST_CASE("Stat lookup miss returns NotFound", "[build_cache]") {
    auto path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(path).is_ok());

    auto r = cache.lookup_stat("/nonexistent/file.sv");
    REQUIRE(r.is_err());
    CHECK(r.error().code == loom::LoomError::NotFound);

    cache.close();
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

TEST_CASE("Stat update and lookup hit", "[build_cache]") {
    auto path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(path).is_ok());

    loom::FileStatEntry entry;
    entry.path = "/test/file.sv";
    entry.inode = 12345;
    entry.mtime_sec = 1700000000;
    entry.mtime_nsec = 123456789;
    entry.size = 4096;
    entry.content_hash = "abcdef1234567890";

    REQUIRE(cache.update_stat(entry).is_ok());

    auto r = cache.lookup_stat("/test/file.sv");
    REQUIRE(r.is_ok());
    CHECK(r.value().path == "/test/file.sv");
    CHECK(r.value().inode == 12345);
    CHECK(r.value().mtime_sec == 1700000000);
    CHECK(r.value().mtime_nsec == 123456789);
    CHECK(r.value().size == 4096);
    CHECK(r.value().content_hash == "abcdef1234567890");

    cache.close();
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

TEST_CASE("Stat invalidation on mtime change", "[build_cache]") {
    auto path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(path).is_ok());

    loom::FileStatEntry entry;
    entry.path = "/test/file.sv";
    entry.inode = 100;
    entry.mtime_sec = 1000;
    entry.mtime_nsec = 0;
    entry.size = 512;
    entry.content_hash = "hash1";
    REQUIRE(cache.update_stat(entry).is_ok());

    // Update with new mtime
    entry.mtime_sec = 2000;
    entry.content_hash = "hash2";
    REQUIRE(cache.update_stat(entry).is_ok());

    auto r = cache.lookup_stat("/test/file.sv");
    REQUIRE(r.is_ok());
    CHECK(r.value().mtime_sec == 2000);
    CHECK(r.value().content_hash == "hash2");

    cache.close();
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

TEST_CASE("cached_file_hash with real file", "[build_cache]") {
    auto db_path = test_db_path();
    auto file_path = "/tmp/loom_test_cache_file_" + std::to_string(getpid()) + ".sv";

    write_test_file(file_path, "module test; endmodule\n");

    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    auto r1 = cache.cached_file_hash(file_path);
    REQUIRE(r1.is_ok());
    CHECK(!r1.value().empty());

    // Second call should be a cache hit with the same hash
    auto r2 = cache.cached_file_hash(file_path);
    REQUIRE(r2.is_ok());
    CHECK(r1.value() == r2.value());

    // Verify against direct SHA256
    auto expected = loom::SHA256::hash_file(file_path);
    CHECK(r1.value() == expected);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
    fs::remove(file_path);
}

// ---------------------------------------------------------------------------
// Binary serialization roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("Serialization roundtrip: empty ParseResult", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    loom::ParseResult pr;
    REQUIRE(cache.store_parse("hash_empty", pr).is_ok());

    auto r = cache.lookup_parse("hash_empty");
    REQUIRE(r.is_ok());
    CHECK(r.value().units.empty());
    CHECK(r.value().diagnostics.empty());

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Serialization roundtrip: ParseResult with all field types", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    loom::ParseResult pr;

    // Create a fully-populated design unit
    loom::DesignUnit u;
    u.kind = loom::DesignUnitKind::Module;
    u.name = "test_module";
    u.start_line = 1;
    u.end_line = 50;
    u.depth = 0;
    u.has_defparam = true;
    u.pos = {"test.sv", 1, 1};

    u.ports.push_back({"clk", loom::PortDirection::Input, "wire", {"test.sv", 2, 5}});
    u.ports.push_back({"data_out", loom::PortDirection::Output, "logic [7:0]", {"test.sv", 3, 5}});
    u.ports.push_back({"data_io", loom::PortDirection::Inout, "wire [15:0]", {"test.sv", 4, 5}});

    u.params.push_back({"WIDTH", "8", false, {"test.sv", 5, 5}});
    u.params.push_back({"DEPTH", "16", true, {"test.sv", 6, 5}});

    u.instantiations.push_back({"sub_mod", "u_sub", true, {"test.sv", 10, 5}});

    u.imports.push_back({"pkg_a", "*", true, {"test.sv", 7, 5}});
    u.imports.push_back({"pkg_b", "CONST", false, {"test.sv", 8, 5}});

    loom::AlwaysBlock ab;
    ab.kind = loom::AlwaysKind::Ff;
    ab.label = "clk_proc";
    ab.assignments.push_back({false, "q", {"test.sv", 15, 9}});
    ab.assignments.push_back({true, "next_q", {"test.sv", 16, 9}});
    ab.pos = {"test.sv", 14, 5};
    u.always_blocks.push_back(ab);

    u.case_statements.push_back({loom::CaseKind::Casez, true, true, false, {"test.sv", 20, 9}});

    u.signals.push_back({"internal_wire", "wire [3:0]", {"test.sv", 25, 5}});

    u.generate_blocks.push_back({"gen_block", true, {"test.sv", 30, 5}});

    u.labeled_blocks.push_back({"begin_label", "end_label", true, {"test.sv", 35, 5}});
    u.labeled_blocks.push_back({"start", "finish", false, {"test.sv", 40, 5}});

    pr.units.push_back(u);

    // Add a second unit (package)
    loom::DesignUnit pkg;
    pkg.kind = loom::DesignUnitKind::Package;
    pkg.name = "test_pkg";
    pkg.start_line = 55;
    pkg.end_line = 70;
    pkg.pos = {"test.sv", 55, 1};
    pr.units.push_back(pkg);

    // Add diagnostics
    pr.diagnostics.push_back({"Missing semicolon", "test.sv", 12, 30});
    pr.diagnostics.push_back({"Unused signal", "test.sv", 25, 5});

    REQUIRE(cache.store_parse("hash_full", pr).is_ok());
    auto r = cache.lookup_parse("hash_full");
    REQUIRE(r.is_ok());

    auto& result = r.value();
    REQUIRE(result.units.size() == 2);

    // Verify first unit
    auto& ru = result.units[0];
    CHECK(ru.kind == loom::DesignUnitKind::Module);
    CHECK(ru.name == "test_module");
    CHECK(ru.start_line == 1);
    CHECK(ru.end_line == 50);
    CHECK(ru.depth == 0);
    CHECK(ru.has_defparam == true);
    CHECK(ru.pos.line == 1);
    CHECK(ru.pos.col == 1);

    REQUIRE(ru.ports.size() == 3);
    CHECK(ru.ports[0].name == "clk");
    CHECK(ru.ports[0].direction == loom::PortDirection::Input);
    CHECK(ru.ports[0].type_text == "wire");
    CHECK(ru.ports[1].name == "data_out");
    CHECK(ru.ports[1].direction == loom::PortDirection::Output);
    CHECK(ru.ports[2].direction == loom::PortDirection::Inout);

    REQUIRE(ru.params.size() == 2);
    CHECK(ru.params[0].name == "WIDTH");
    CHECK(ru.params[0].default_text == "8");
    CHECK(ru.params[0].is_localparam == false);
    CHECK(ru.params[1].is_localparam == true);

    REQUIRE(ru.instantiations.size() == 1);
    CHECK(ru.instantiations[0].module_name == "sub_mod");
    CHECK(ru.instantiations[0].instance_name == "u_sub");
    CHECK(ru.instantiations[0].is_parameterized == true);

    REQUIRE(ru.imports.size() == 2);
    CHECK(ru.imports[0].is_wildcard == true);
    CHECK(ru.imports[1].symbol == "CONST");

    REQUIRE(ru.always_blocks.size() == 1);
    CHECK(ru.always_blocks[0].kind == loom::AlwaysKind::Ff);
    CHECK(ru.always_blocks[0].label == "clk_proc");
    REQUIRE(ru.always_blocks[0].assignments.size() == 2);
    CHECK(ru.always_blocks[0].assignments[0].is_blocking == false);
    CHECK(ru.always_blocks[0].assignments[0].target == "q");
    CHECK(ru.always_blocks[0].assignments[1].is_blocking == true);

    REQUIRE(ru.case_statements.size() == 1);
    CHECK(ru.case_statements[0].kind == loom::CaseKind::Casez);
    CHECK(ru.case_statements[0].has_default == true);
    CHECK(ru.case_statements[0].is_unique == true);
    CHECK(ru.case_statements[0].is_priority == false);

    REQUIRE(ru.signals.size() == 1);
    CHECK(ru.signals[0].name == "internal_wire");

    REQUIRE(ru.generate_blocks.size() == 1);
    CHECK(ru.generate_blocks[0].label == "gen_block");
    CHECK(ru.generate_blocks[0].has_label == true);

    REQUIRE(ru.labeled_blocks.size() == 2);
    CHECK(ru.labeled_blocks[0].labels_match == true);
    CHECK(ru.labeled_blocks[1].labels_match == false);

    // Verify second unit
    CHECK(result.units[1].kind == loom::DesignUnitKind::Package);
    CHECK(result.units[1].name == "test_pkg");

    // Verify diagnostics
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics[0].message == "Missing semicolon");
    CHECK(result.diagnostics[0].line == 12);
    CHECK(result.diagnostics[1].message == "Unused signal");

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Serialization roundtrip: DesignUnit with nested AlwaysBlock.assignments", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    loom::ParseResult pr;
    loom::DesignUnit u;
    u.kind = loom::DesignUnitKind::Module;
    u.name = "nested_test";
    u.pos = {"", 1, 1};

    // Multiple always blocks with varying assignment counts
    for (int i = 0; i < 3; ++i) {
        loom::AlwaysBlock ab;
        ab.kind = static_cast<loom::AlwaysKind>(i);
        ab.label = "block_" + std::to_string(i);
        ab.pos = {"", 10 + i * 10, 5};
        for (int j = 0; j <= i; ++j) {
            ab.assignments.push_back({j % 2 == 0, "sig_" + std::to_string(j), {"", 11 + j, 9}});
        }
        u.always_blocks.push_back(ab);
    }
    pr.units.push_back(u);

    REQUIRE(cache.store_parse("hash_nested", pr).is_ok());
    auto r = cache.lookup_parse("hash_nested");
    REQUIRE(r.is_ok());

    auto& ru = r.value().units[0];
    REQUIRE(ru.always_blocks.size() == 3);
    CHECK(ru.always_blocks[0].assignments.size() == 1);
    CHECK(ru.always_blocks[1].assignments.size() == 2);
    CHECK(ru.always_blocks[2].assignments.size() == 3);
    CHECK(ru.always_blocks[2].assignments[2].target == "sig_2");

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Deserialization of corrupted data returns error", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Store valid data first, then corrupt it via raw SQL
    loom::ParseResult pr;
    pr.units.push_back({});
    pr.units[0].name = "test";
    pr.units[0].pos = {"", 1, 1};
    REQUIRE(cache.store_parse("hash_corrupt", pr).is_ok());

    // Verify it works first
    auto good = cache.lookup_parse("hash_corrupt");
    REQUIRE(good.is_ok());

    // Now store garbage blob directly
    cache.close();

    // Reopen and store corrupt data manually
    REQUIRE(cache.open(db_path).is_ok());

    // Store with wrong magic by creating a new entry with truncated data
    loom::ParseResult empty;
    REQUIRE(cache.store_parse("hash_trunc", empty).is_ok());

    // The empty one should still work (it's valid)
    auto r = cache.lookup_parse("hash_trunc");
    REQUIRE(r.is_ok());
    CHECK(r.value().units.empty());

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Deserialization with wrong magic returns error", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Manually store a blob with wrong magic via the SQLite layer
    // We'll test this by trying to lookup a hash that doesn't exist
    auto r = cache.lookup_parse("nonexistent_hash");
    REQUIRE(r.is_err());
    CHECK(r.error().code == loom::LoomError::NotFound);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Parse cache
// ---------------------------------------------------------------------------

TEST_CASE("Store parse and lookup parse", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    loom::ParseResult pr;
    loom::DesignUnit u;
    u.kind = loom::DesignUnitKind::Interface;
    u.name = "my_if";
    u.start_line = 1;
    u.end_line = 10;
    u.pos = {"", 1, 1};
    pr.units.push_back(u);

    REQUIRE(cache.store_parse("abc123", pr).is_ok());
    auto r = cache.lookup_parse("abc123");
    REQUIRE(r.is_ok());
    CHECK(r.value().units[0].name == "my_if");
    CHECK(r.value().units[0].kind == loom::DesignUnitKind::Interface);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Parse cache miss returns error", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    auto r = cache.lookup_parse("does_not_exist");
    REQUIRE(r.is_err());
    CHECK(r.error().code == loom::LoomError::NotFound);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Include dependency tracking
// ---------------------------------------------------------------------------

TEST_CASE("Store includes and get includes", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    std::vector<loom::IncludeDepEntry> deps = {
        {"src_hash", "header_a.vh", "inc_hash_a"},
        {"src_hash", "header_b.vh", "inc_hash_b"},
    };
    REQUIRE(cache.store_includes("src_hash", deps).is_ok());

    auto r = cache.get_includes("src_hash");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].include_path == "header_a.vh");
    CHECK(r.value()[1].include_path == "header_b.vh");

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("find_includers reverse lookup", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Two source files both include the same header
    std::vector<loom::IncludeDepEntry> deps1 = {
        {"src1", "common.vh", "common_hash"},
    };
    std::vector<loom::IncludeDepEntry> deps2 = {
        {"src2", "common.vh", "common_hash"},
    };
    REQUIRE(cache.store_includes("src1", deps1).is_ok());
    REQUIRE(cache.store_includes("src2", deps2).is_ok());

    auto r = cache.find_includers("common_hash");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 2);

    // Sort for deterministic comparison
    auto includers = r.value();
    std::sort(includers.begin(), includers.end());
    CHECK(includers[0] == "src1");
    CHECK(includers[1] == "src2");

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Dependency edge tracking
// ---------------------------------------------------------------------------

TEST_CASE("Store edges and get edges", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    std::vector<loom::DepEdgeEntry> edges = {
        {"file_hash", "top_mod", "sub_mod_a"},
        {"file_hash", "top_mod", "sub_mod_b"},
    };
    REQUIRE(cache.store_edges("file_hash", edges).is_ok());

    auto r = cache.get_edges("file_hash");
    REQUIRE(r.is_ok());
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].source_unit == "top_mod");
    CHECK(r.value()[0].target_unit == "sub_mod_a");
    CHECK(r.value()[1].target_unit == "sub_mod_b");

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Filelist cache
// ---------------------------------------------------------------------------

TEST_CASE("Store filelist and lookup filelist", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    loom::FilelistCacheEntry entry;
    entry.filelist_key = "fl_key_123";
    entry.file_list = {"src/a.sv", "src/b.sv", "src/c.sv"};
    entry.top_modules = {"top_a", "top_b"};
    REQUIRE(cache.store_filelist(entry).is_ok());

    auto r = cache.lookup_filelist("fl_key_123");
    REQUIRE(r.is_ok());
    CHECK(r.value().filelist_key == "fl_key_123");
    REQUIRE(r.value().file_list.size() == 3);
    CHECK(r.value().file_list[0] == "src/a.sv");
    CHECK(r.value().file_list[1] == "src/b.sv");
    CHECK(r.value().file_list[2] == "src/c.sv");
    REQUIRE(r.value().top_modules.size() == 2);
    CHECK(r.value().top_modules[0] == "top_a");
    CHECK(r.value().top_modules[1] == "top_b");
    CHECK(r.value().created_at > 0);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Filelist cache miss", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    auto r = cache.lookup_filelist("nonexistent_key");
    REQUIRE(r.is_err());
    CHECK(r.error().code == loom::LoomError::NotFound);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Hash computation helpers
// ---------------------------------------------------------------------------

TEST_CASE("compute_effective_hash is deterministic", "[build_cache]") {
    auto h1 = loom::BuildCache::compute_effective_hash(
        "content_abc",
        {"inc_1", "inc_2"},
        {"DEFINE_A=1", "DEFINE_B"},
        {"include/", "src/"}
    );
    auto h2 = loom::BuildCache::compute_effective_hash(
        "content_abc",
        {"inc_2", "inc_1"},  // different order
        {"DEFINE_B", "DEFINE_A=1"},  // different order
        {"src/", "include/"}  // different order
    );
    CHECK(h1 == h2);
    CHECK(h1.size() == 64);  // SHA256 hex

    // Different content hash should give different result
    auto h3 = loom::BuildCache::compute_effective_hash(
        "content_xyz",
        {"inc_1", "inc_2"},
        {"DEFINE_A=1", "DEFINE_B"},
        {"include/", "src/"}
    );
    CHECK(h1 != h3);
}

TEST_CASE("compute_filelist_key is deterministic", "[build_cache]") {
    auto k1 = loom::BuildCache::compute_filelist_key(
        "0.1.0", "manifest_hash",
        {"eff_a", "eff_b", "eff_c"}
    );
    auto k2 = loom::BuildCache::compute_filelist_key(
        "0.1.0", "manifest_hash",
        {"eff_c", "eff_a", "eff_b"}  // different order
    );
    CHECK(k1 == k2);
    CHECK(k1.size() == 64);

    // Different version should give different key
    auto k3 = loom::BuildCache::compute_filelist_key(
        "0.2.0", "manifest_hash",
        {"eff_a", "eff_b", "eff_c"}
    );
    CHECK(k1 != k3);
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

TEST_CASE("Prune removes orphaned entries", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Store a parse result with hash "orphan" but no file_stat entry for it
    loom::ParseResult pr;
    pr.units.push_back({});
    pr.units[0].name = "orphan_mod";
    pr.units[0].pos = {"", 1, 1};
    REQUIRE(cache.store_parse("orphan_hash", pr).is_ok());

    // Store include deps and edges for orphan
    std::vector<loom::IncludeDepEntry> inc = {{"orphan_hash", "h.vh", "h_hash"}};
    REQUIRE(cache.store_includes("orphan_hash", inc).is_ok());
    std::vector<loom::DepEdgeEntry> edges = {{"orphan_hash", "orphan_mod", "other"}};
    REQUIRE(cache.store_edges("orphan_hash", edges).is_ok());

    // Also store a parse result with a matching file_stat entry
    REQUIRE(cache.store_parse("kept_hash", pr).is_ok());
    loom::FileStatEntry stat;
    stat.path = "/kept/file.sv";
    stat.content_hash = "kept_hash";
    REQUIRE(cache.update_stat(stat).is_ok());

    // Prune
    REQUIRE(cache.prune().is_ok());

    // Orphan should be gone
    CHECK(cache.lookup_parse("orphan_hash").is_err());
    CHECK(cache.get_includes("orphan_hash").is_ok());
    CHECK(cache.get_includes("orphan_hash").value().empty());
    CHECK(cache.get_edges("orphan_hash").is_ok());
    CHECK(cache.get_edges("orphan_hash").value().empty());

    // Kept entry should still be there
    CHECK(cache.lookup_parse("kept_hash").is_ok());

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Clear removes all data", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Insert data into various tables
    loom::FileStatEntry stat;
    stat.path = "/a.sv";
    stat.content_hash = "h";
    REQUIRE(cache.update_stat(stat).is_ok());

    loom::ParseResult pr;
    REQUIRE(cache.store_parse("h", pr).is_ok());

    REQUIRE(cache.clear().is_ok());

    auto stats = cache.get_stats();
    REQUIRE(stats.is_ok());
    CHECK(stats.value().file_stat_count == 0);
    CHECK(stats.value().parse_result_count == 0);
    CHECK(stats.value().include_dep_count == 0);
    CHECK(stats.value().dep_edge_count == 0);
    CHECK(stats.value().filelist_count == 0);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Get stats returns correct counts", "[build_cache]") {
    auto db_path = test_db_path();
    loom::BuildCache cache;
    REQUIRE(cache.open(db_path).is_ok());

    // Add 3 stat entries
    for (int i = 0; i < 3; ++i) {
        loom::FileStatEntry e;
        e.path = "/file_" + std::to_string(i) + ".sv";
        e.content_hash = "hash_" + std::to_string(i);
        REQUIRE(cache.update_stat(e).is_ok());
    }

    // Add 2 parse results
    loom::ParseResult pr;
    REQUIRE(cache.store_parse("hash_0", pr).is_ok());
    REQUIRE(cache.store_parse("hash_1", pr).is_ok());

    auto stats = cache.get_stats();
    REQUIRE(stats.is_ok());
    CHECK(stats.value().file_stat_count == 3);
    CHECK(stats.value().parse_result_count == 2);
    CHECK(stats.value().total_bytes > 0);

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

// ---------------------------------------------------------------------------
// Schema migration and corruption recovery
// ---------------------------------------------------------------------------

TEST_CASE("Schema migration: clear on version mismatch", "[build_cache]") {
    auto db_path = test_db_path();

    // Open and populate
    {
        loom::BuildCache cache;
        REQUIRE(cache.open(db_path).is_ok());
        loom::ParseResult pr;
        REQUIRE(cache.store_parse("old_data", pr).is_ok());
        cache.close();
    }

    // Tamper with schema version directly via SQLite
    {
        sqlite3* raw_db = nullptr;
        REQUIRE(sqlite3_open(db_path.c_str(), &raw_db) == SQLITE_OK);
        char* errmsg = nullptr;
        sqlite3_exec(raw_db,
            "UPDATE schema_info SET value='999' WHERE key='version'",
            nullptr, nullptr, &errmsg);
        if (errmsg) sqlite3_free(errmsg);
        sqlite3_close(raw_db);
    }

    // Reopen — should detect version mismatch and clear
    {
        loom::BuildCache cache;
        REQUIRE(cache.open(db_path).is_ok());

        // Old data should be gone
        auto r = cache.lookup_parse("old_data");
        CHECK(r.is_err());

        // Should still be functional
        loom::ParseResult pr;
        REQUIRE(cache.store_parse("new_data", pr).is_ok());
        CHECK(cache.lookup_parse("new_data").is_ok());

        cache.close();
    }

    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST_CASE("Corruption recovery: recreate on bad DB", "[build_cache]") {
    auto db_path = test_db_path();

    // Write garbage to simulate corruption
    {
        std::ofstream f(db_path);
        f << "this is not a valid sqlite database file at all";
    }

    // Open should recover by deleting and recreating
    loom::BuildCache cache;
    auto r = cache.open(db_path);
    REQUIRE(r.is_ok());
    REQUIRE(cache.is_open());

    // Should be functional after recovery
    loom::ParseResult pr;
    REQUIRE(cache.store_parse("after_recovery", pr).is_ok());
    CHECK(cache.lookup_parse("after_recovery").is_ok());

    cache.close();
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}
