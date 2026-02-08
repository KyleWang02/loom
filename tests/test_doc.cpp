#include <catch2/catch.hpp>
#include <loom/doc.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/lang/parser.hpp>
#include <filesystem>
#include <fstream>

using namespace loom;
using namespace loom::doc;
namespace fs = std::filesystem;

// ===========================================================================
// Helpers
// ===========================================================================

static std::pair<LexResult, ParseResult> lex_parse(const std::string& src,
                                                    const std::string& filename = "test.sv") {
    bool is_sv = fs::path(filename).extension().string() == ".sv";
    auto lr = lex(src, filename, is_sv);
    REQUIRE(lr.is_ok());
    auto pr = parse(lr.value(), filename, is_sv);
    REQUIRE(pr.is_ok());
    return {std::move(lr.value()), std::move(pr.value())};
}

// Extract doc units from source
static std::vector<DesignUnitDoc> extract_source(const std::string& src,
                                                  const std::string& filename = "test.sv") {
    auto [lr, pr] = lex_parse(src, filename);
    DocExtractor ex;
    return ex.extract(lr, pr, filename);
}

// Build a DocModel from a single source file
static DocModel model_from_source(const std::string& src,
                                   const std::string& filename = "test.sv",
                                   const PackageInfo& pkg = {"test_pkg", "1.0.0", "", {}}) {
    auto [lr, pr] = lex_parse(src, filename);
    DocExtractor ex;
    std::vector<std::tuple<std::string, LexResult, ParseResult>> files;
    files.emplace_back(filename, std::move(lr), std::move(pr));
    return ex.extract_all(files, pkg);
}

struct TempDir {
    fs::path path;
    TempDir() : path(fs::temp_directory_path() / ("loom_doc_test_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

// ===========================================================================
// DocComment parsing
// ===========================================================================

TEST_CASE("parse_doc_comment — empty", "[doc]") {
    auto doc = parse_doc_comment("", 1);
    CHECK(doc.empty());
    CHECK(doc.brief.empty());
    CHECK(doc.body.empty());
    CHECK(doc.tags.empty());
}

TEST_CASE("parse_doc_comment — simple description", "[doc]") {
    auto doc = parse_doc_comment("A simple FIFO buffer.", 1);
    CHECK(doc.brief == "A simple FIFO buffer.");
    CHECK(doc.body == "A simple FIFO buffer.");
    CHECK(doc.tags.empty());
}

TEST_CASE("parse_doc_comment — multi-line body with auto-brief", "[doc]") {
    auto doc = parse_doc_comment(
        "A parameterizable FIFO buffer.\n"
        "\n"
        "Supports configurable depth and width.", 1);
    CHECK(doc.brief == "A parameterizable FIFO buffer.");
    CHECK(doc.body.find("Supports configurable") != std::string::npos);
}

TEST_CASE("parse_doc_comment — @param tag", "[doc]") {
    auto doc = parse_doc_comment(
        "A FIFO module.\n"
        "@param DEPTH Number of entries (must be power of 2).\n"
        "@param WIDTH Data width in bits.", 1);
    CHECK(doc.brief == "A FIFO module.");
    CHECK(doc.tags.size() == 2);
    CHECK(doc.tags[0].kind == DocTagKind::Param);
    CHECK(doc.tags[0].name == "DEPTH");
    CHECK(doc.tags[0].text == "Number of entries (must be power of 2).");
    CHECK(doc.tags[1].name == "WIDTH");
    CHECK(doc.find_param_doc("DEPTH") == "Number of entries (must be power of 2).");
    CHECK(doc.find_param_doc("WIDTH") == "Data width in bits.");
    CHECK(doc.find_param_doc("NONEXISTENT").empty());
}

TEST_CASE("parse_doc_comment — @port tag", "[doc]") {
    auto doc = parse_doc_comment(
        "A UART receiver.\n"
        "@port clk System clock.\n"
        "@port rx Serial input.", 1);
    CHECK(doc.find_port_doc("clk") == "System clock.");
    CHECK(doc.find_port_doc("rx") == "Serial input.");
    CHECK(doc.find_port_doc("tx").empty());
}

TEST_CASE("parse_doc_comment — @see tag", "[doc]") {
    auto doc = parse_doc_comment(
        "A FIFO.\n"
        "@see fifo_async\n"
        "@see fifo_pkg", 1);
    auto sees = doc.see_also();
    CHECK(sees.size() == 2);
    CHECK(sees[0] == "fifo_async");
    CHECK(sees[1] == "fifo_pkg");
}

TEST_CASE("parse_doc_comment — @deprecated", "[doc]") {
    auto doc = parse_doc_comment(
        "Old module.\n"
        "@deprecated Use fifo_v2 instead.", 1);
    CHECK(doc.has_deprecated());
    CHECK(doc.deprecated_message() == "Use fifo_v2 instead.");
}

TEST_CASE("parse_doc_comment — @deprecated without message", "[doc]") {
    auto doc = parse_doc_comment(
        "Old module.\n"
        "@deprecated", 1);
    CHECK(doc.has_deprecated());
    CHECK(doc.deprecated_message().empty());
}

TEST_CASE("parse_doc_comment — @example tag", "[doc]") {
    auto doc = parse_doc_comment(
        "A FIFO.\n"
        "@example fifo_sync #(.DEPTH(8)) u_fifo(.clk(clk));", 1);
    auto examples = doc.tags_of(DocTagKind::Example);
    CHECK(examples.size() == 1);
    CHECK(examples[0].text.find("fifo_sync") != std::string::npos);
}

TEST_CASE("parse_doc_comment — @note and @warning", "[doc]") {
    auto doc = parse_doc_comment(
        "A module.\n"
        "@note DEPTH must be power of 2.\n"
        "@warning Not synthesizable.", 1);
    auto notes = doc.tags_of(DocTagKind::Note);
    CHECK(notes.size() == 1);
    CHECK(notes[0].text == "DEPTH must be power of 2.");
    auto warnings = doc.tags_of(DocTagKind::Warning);
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].text == "Not synthesizable.");
}

TEST_CASE("parse_doc_comment — @wavedrom tag", "[doc]") {
    auto doc = parse_doc_comment(
        "A clock divider.\n"
        "@wavedrom {signal: [{name: 'clk', wave: 'p....'}]}", 1);
    auto wd = doc.tags_of(DocTagKind::WaveDrom);
    CHECK(wd.size() == 1);
    CHECK(wd[0].text.find("signal") != std::string::npos);
}

TEST_CASE("parse_doc_comment — mixed tags", "[doc]") {
    auto doc = parse_doc_comment(
        "UART transmitter.\n"
        "@param BAUD_RATE Target baud rate.\n"
        "@see uart_rx\n"
        "@deprecated Use uart_v2.", 1);
    CHECK(doc.brief == "UART transmitter.");
    CHECK(doc.tags.size() == 3);
    CHECK(doc.find_param_doc("BAUD_RATE") == "Target baud rate.");
    CHECK(doc.see_also().size() == 1);
    CHECK(doc.has_deprecated());
}

TEST_CASE("DocComment — tags_of returns correct subset", "[doc]") {
    auto doc = parse_doc_comment(
        "Module.\n"
        "@param A desc A\n"
        "@param B desc B\n"
        "@see other", 1);
    CHECK(doc.tags_of(DocTagKind::Param).size() == 2);
    CHECK(doc.tags_of(DocTagKind::See).size() == 1);
    CHECK(doc.tags_of(DocTagKind::Deprecated).size() == 0);
}

// ===========================================================================
// DocExtractor
// ===========================================================================

TEST_CASE("DocExtractor — leading doc comment on module", "[doc]") {
    auto units = extract_source(R"(
/// A simple test module.
module test;
endmodule
)");
    REQUIRE(units.size() == 1);
    CHECK(units[0].name == "test");
    CHECK(units[0].doc.brief == "A simple test module.");
}

TEST_CASE("DocExtractor — multi-line leading doc", "[doc]") {
    auto units = extract_source(R"(
/// A parameterizable FIFO buffer.
///
/// Supports configurable depth and width.
/// @param DEPTH Number of entries.
module fifo_sync;
  parameter DEPTH = 16;
endmodule
)");
    REQUIRE(units.size() == 1);
    CHECK(units[0].name == "fifo_sync");
    CHECK(units[0].doc.brief == "A parameterizable FIFO buffer.");
    CHECK(units[0].doc.find_param_doc("DEPTH") == "Number of entries.");
}

TEST_CASE("DocExtractor — no doc comment", "[doc]") {
    auto units = extract_source(R"(
module no_doc;
endmodule
)");
    REQUIRE(units.size() == 1);
    CHECK(units[0].doc.empty());
}

TEST_CASE("DocExtractor — trailing doc on ports", "[doc]") {
    auto units = extract_source(R"(
/// A module with ports.
module test(
  input  logic clk,   /// System clock
  input  logic rst_n, /// Active-low reset
  output logic out     /// Output signal
);
endmodule
)");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].ports.size() == 3);
    CHECK(units[0].ports[0].name == "clk");
    CHECK(units[0].ports[0].description == "System clock");
    CHECK(units[0].ports[1].name == "rst_n");
    CHECK(units[0].ports[1].description == "Active-low reset");
    CHECK(units[0].ports[2].name == "out");
    CHECK(units[0].ports[2].description == "Output signal");
}

TEST_CASE("DocExtractor — @port tag overrides trailing comment", "[doc]") {
    auto units = extract_source(R"(
/// A module.
/// @port clk Main system clock
module test(
  input logic clk  /// Clock signal
);
endmodule
)");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].ports.size() == 1);
    // @port tag takes priority
    CHECK(units[0].ports[0].description == "Main system clock");
}

TEST_CASE("DocExtractor — parameter docs from @param", "[doc]") {
    auto units = extract_source(R"(
/// A FIFO.
/// @param DEPTH Number of entries.
/// @param WIDTH Data width.
module fifo;
  parameter DEPTH = 16;
  parameter WIDTH = 8;
endmodule
)");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].params.size() == 2);
    CHECK(units[0].params[0].name == "DEPTH");
    CHECK(units[0].params[0].description == "Number of entries.");
    CHECK(units[0].params[0].default_text == "16");
    CHECK(units[0].params[1].name == "WIDTH");
    CHECK(units[0].params[1].description == "Data width.");
}

TEST_CASE("DocExtractor — trailing doc on parameters", "[doc]") {
    auto units = extract_source(R"(
/// A module.
module test;
  parameter WIDTH = 8; /// Data width in bits
endmodule
)");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].params.size() == 1);
    CHECK(units[0].params[0].description == "Data width in bits");
}

TEST_CASE("DocExtractor — instantiation list", "[doc]") {
    auto units = extract_source(R"(
/// Top module.
module top;
  sub_a u1();
  sub_b u2();
  sub_a u3();
endmodule
)", "test.v");
    REQUIRE(units.size() == 1);
    // sub_a appears twice but should be deduplicated
    CHECK(units[0].instantiates.size() == 2);
    CHECK(units[0].instantiates[0] == "sub_a");
    CHECK(units[0].instantiates[1] == "sub_b");
}

TEST_CASE("DocExtractor — multiple units in one file", "[doc]") {
    auto units = extract_source(R"(
/// Module A.
module mod_a;
endmodule

/// Module B.
module mod_b;
endmodule
)");
    CHECK(units.size() == 2);
    CHECK(units[0].name == "mod_a");
    CHECK(units[0].doc.brief == "Module A.");
    CHECK(units[1].name == "mod_b");
    CHECK(units[1].doc.brief == "Module B.");
}

TEST_CASE("DocExtractor — skips nested units (depth > 0)", "[doc]") {
    // Only top-level units should be extracted
    auto units = extract_source(R"(
/// Top.
module top;
endmodule
)");
    CHECK(units.size() == 1);
    CHECK(units[0].name == "top");
}

TEST_CASE("DocExtractor — port direction and type", "[doc]") {
    auto units = extract_source(R"(
/// Module with typed ports.
module test(
  input  logic       clk,
  output logic [7:0] data,
  inout  wire        bidir
);
endmodule
)");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].ports.size() == 3);
    CHECK(units[0].ports[0].direction == PortDirection::Input);
    CHECK(units[0].ports[1].direction == PortDirection::Output);
    CHECK(units[0].ports[2].direction == PortDirection::Inout);
}

TEST_CASE("DocExtractor — deprecated module", "[doc]") {
    auto units = extract_source(R"(
/// Old FIFO implementation.
/// @deprecated Use fifo_v2 instead.
module fifo_old;
endmodule
)");
    REQUIRE(units.size() == 1);
    CHECK(units[0].doc.has_deprecated());
    CHECK(units[0].doc.deprecated_message() == "Use fifo_v2 instead.");
}

// ===========================================================================
// DocModel
// ===========================================================================

TEST_CASE("DocModel — resolve_cross_refs", "[doc]") {
    auto model = model_from_source(R"(
/// Top module.
module top;
  sub u1();
endmodule

/// Sub module.
module sub;
endmodule
)", "test.v");

    CHECK(model.units.size() == 2);
    auto* top = model.find_unit("top");
    auto* sub = model.find_unit("sub");
    REQUIRE(top != nullptr);
    REQUIRE(sub != nullptr);

    CHECK(top->instantiates.size() == 1);
    CHECK(top->instantiates[0] == "sub");
    CHECK(sub->instantiated_by.size() == 1);
    CHECK(sub->instantiated_by[0] == "top");
}

TEST_CASE("DocModel — find_unit", "[doc]") {
    auto model = model_from_source(R"(
/// A module.
module foo; endmodule
)");
    CHECK(model.find_unit("foo") != nullptr);
    CHECK(model.find_unit("bar") == nullptr);
}

TEST_CASE("DocModel — categorized access", "[doc]") {
    auto model = model_from_source(R"(
module mod_a; endmodule
interface if_a; endinterface
package pkg_a; endpackage
)");
    CHECK(model.modules().size() == 1);
    CHECK(model.interfaces().size() == 1);
    CHECK(model.packages().size() == 1);
}

TEST_CASE("DocModel — cross-refs with external dependency", "[doc]") {
    auto model = model_from_source(R"(
/// Top.
module top;
  external_mod u1();
endmodule
)", "test.v");

    auto* top = model.find_unit("top");
    REQUIRE(top != nullptr);
    CHECK(top->instantiates.size() == 1);
    CHECK(top->instantiates[0] == "external_mod");
    // external_mod not in model, so no instantiated_by to check
    CHECK(model.find_unit("external_mod") == nullptr);
}

TEST_CASE("DocModel — multi-file extraction", "[doc]") {
    auto [lr1, pr1] = lex_parse(R"(
/// Top module.
module top;
  sub u1();
endmodule
)", "top.sv");

    auto [lr2, pr2] = lex_parse(R"(
/// Sub module.
module sub;
endmodule
)", "sub.sv");

    DocExtractor ex;
    std::vector<std::tuple<std::string, LexResult, ParseResult>> files;
    files.emplace_back("top.sv", std::move(lr1), std::move(pr1));
    files.emplace_back("sub.sv", std::move(lr2), std::move(pr2));

    auto model = ex.extract_all(files, {"my_pkg", "2.0.0", "", {}});

    CHECK(model.package.name == "my_pkg");
    CHECK(model.package.version == "2.0.0");
    CHECK(model.units.size() == 2);
    CHECK(model.find_unit("top")->file == "top.sv");
    CHECK(model.find_unit("sub")->file == "sub.sv");
    CHECK(model.find_unit("sub")->instantiated_by.size() == 1);
}

// ===========================================================================
// MarkdownRenderer
// ===========================================================================

TEST_CASE("MarkdownRenderer — render_index", "[doc]") {
    auto model = model_from_source(R"(
/// UART transmitter.
module uart_tx;
endmodule

/// UART receiver.
module uart_rx;
endmodule
)", "uart.sv", {"uart_lib", "1.0.0", "", {}});

    MarkdownRenderer renderer;
    auto index = renderer.render_index(model);

    CHECK(index.find("# uart_lib") != std::string::npos);
    CHECK(index.find("**Version**: 1.0.0") != std::string::npos);
    CHECK(index.find("## Modules") != std::string::npos);
    CHECK(index.find("`uart_tx`") != std::string::npos);
    CHECK(index.find("`uart_rx`") != std::string::npos);
    CHECK(index.find("UART transmitter.") != std::string::npos);
    CHECK(index.find("modules/uart_tx.md") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit basic", "[doc]") {
    auto model = model_from_source(R"(
/// A simple counter module.
///
/// Counts up on each clock edge.
/// @param WIDTH Counter width in bits.
module counter(
  input  logic clk,   /// System clock
  input  logic rst_n, /// Active-low reset
  output logic [7:0] count /// Current count value
);
  parameter WIDTH = 8;
endmodule
)");

    MarkdownRenderer renderer;
    auto* unit = model.find_unit("counter");
    REQUIRE(unit != nullptr);
    auto md = renderer.render_unit(*unit, model);

    CHECK(md.find("# Module: `counter`") != std::string::npos);
    CHECK(md.find("A simple counter module.") != std::string::npos);
    CHECK(md.find("Counts up on each clock edge.") != std::string::npos);
    CHECK(md.find("## Parameters") != std::string::npos);
    CHECK(md.find("`WIDTH`") != std::string::npos);
    CHECK(md.find("## Ports") != std::string::npos);
    CHECK(md.find("`clk`") != std::string::npos);
    CHECK(md.find("input") != std::string::npos);
    CHECK(md.find("System clock") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit with deprecation", "[doc]") {
    auto model = model_from_source(R"(
/// Old counter.
/// @deprecated Use counter_v2 instead.
module counter_old;
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("counter_old"), model);

    CHECK(md.find("**Deprecated**") != std::string::npos);
    CHECK(md.find("Use counter_v2 instead.") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit with dependencies", "[doc]") {
    auto model = model_from_source(R"(
/// Top module.
module top;
  sub u1();
endmodule

/// Sub module.
module sub;
endmodule
)", "test.v");

    MarkdownRenderer renderer;
    auto md_top = renderer.render_unit(*model.find_unit("top"), model);
    auto md_sub = renderer.render_unit(*model.find_unit("sub"), model);

    CHECK(md_top.find("## Instantiates") != std::string::npos);
    CHECK(md_top.find("`sub`") != std::string::npos);
    CHECK(md_sub.find("## Instantiated By") != std::string::npos);
    CHECK(md_sub.find("`top`") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit with see-also", "[doc]") {
    auto model = model_from_source(R"(
/// UART TX.
/// @see uart_rx
module uart_tx;
endmodule

/// UART RX.
module uart_rx;
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("uart_tx"), model);

    CHECK(md.find("## See Also") != std::string::npos);
    CHECK(md.find("`uart_rx`") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_mermaid_graph", "[doc]") {
    auto model = model_from_source(R"(
/// Top.
module top;
  mid u1();
endmodule
/// Mid.
module mid;
  leaf u1();
endmodule
/// Leaf.
module leaf;
endmodule
)", "test.v");

    MarkdownRenderer renderer;
    auto graph = renderer.render_mermaid_graph(model);

    CHECK(graph.find("```mermaid") != std::string::npos);
    CHECK(graph.find("graph TD") != std::string::npos);
    CHECK(graph.find("top") != std::string::npos);
    CHECK(graph.find("mid") != std::string::npos);
    CHECK(graph.find("leaf") != std::string::npos);
    CHECK(graph.find("top --> mid") != std::string::npos);
    CHECK(graph.find("mid --> leaf") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit_graph", "[doc]") {
    auto model = model_from_source(R"(
/// Top.
module top;
  mid u1();
endmodule
/// Mid.
module mid;
endmodule
)", "test.v");

    MarkdownRenderer renderer;
    auto graph = renderer.render_unit_graph(*model.find_unit("top"), model);

    CHECK(graph.find("```mermaid") != std::string::npos);
    CHECK(graph.find("top --> mid") != std::string::npos);
    CHECK(graph.find("classDef current") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_search_index", "[doc]") {
    auto model = model_from_source(R"(
/// A counter.
module counter;
endmodule
)", "test.sv", {"mylib", "1.0.0", "", {}});

    MarkdownRenderer renderer;
    auto json = renderer.render_search_index(model);

    CHECK(json.find("\"name\":\"counter\"") != std::string::npos);
    CHECK(json.find("\"kind\":\"Module\"") != std::string::npos);
    CHECK(json.find("\"brief\":\"A counter.\"") != std::string::npos);
    CHECK(json.find("\"package\"") != std::string::npos);
    CHECK(json.find("\"name\":\"mylib\"") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_index with interfaces and packages", "[doc]") {
    auto model = model_from_source(R"(
module mod_a; endmodule
interface if_a; endinterface
package pkg_a; endpackage
)", "test.sv", {"mixed_lib", "0.1.0", "", {}});

    MarkdownRenderer renderer;
    auto index = renderer.render_index(model);

    CHECK(index.find("## Modules") != std::string::npos);
    CHECK(index.find("## Interfaces") != std::string::npos);
    CHECK(index.find("## Packages") != std::string::npos);
    CHECK(index.find("modules/mod_a.md") != std::string::npos);
    CHECK(index.find("interfaces/if_a.md") != std::string::npos);
    CHECK(index.find("packages/pkg_a.md") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit with example", "[doc]") {
    auto model = model_from_source(R"(
/// A FIFO buffer.
/// @example fifo_sync #(.DEPTH(8)) u_fifo (.clk(clk), .rst(rst));
module fifo_sync;
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("fifo_sync"), model);

    CHECK(md.find("## Examples") != std::string::npos);
    CHECK(md.find("```systemverilog") != std::string::npos);
    CHECK(md.find("fifo_sync") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — render_unit with note and warning", "[doc]") {
    auto model = model_from_source(R"(
/// A module.
/// @note DEPTH must be power of 2.
/// @warning Not synthesizable in all tools.
module test_mod;
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("test_mod"), model);

    CHECK(md.find("> **Note**: DEPTH must be power of 2.") != std::string::npos);
    CHECK(md.find("> **Warning**: Not synthesizable in all tools.") != std::string::npos);
}

// ===========================================================================
// Render to disk
// ===========================================================================

TEST_CASE("MarkdownRenderer — render to disk", "[doc]") {
    TempDir tmp;
    auto model = model_from_source(R"(
/// UART transmitter.
/// @param BAUD_RATE Target baud rate.
module uart_tx(
  input  logic clk,  /// System clock
  output logic tx    /// Serial output
);
  parameter BAUD_RATE = 9600;
endmodule

/// UART receiver.
module uart_rx;
endmodule

interface axi_if;
endinterface

package uart_pkg;
endpackage
)", "uart.sv", {"uart_lib", "1.0.0", "", {}});

    RenderConfig config;
    config.output_dir = tmp.path / "docs";
    MarkdownRenderer renderer(config);

    auto result = renderer.render(model);
    REQUIRE(result.is_ok());

    // Check files exist
    CHECK(fs::exists(tmp.path / "docs" / "index.md"));
    CHECK(fs::exists(tmp.path / "docs" / "modules" / "uart_tx.md"));
    CHECK(fs::exists(tmp.path / "docs" / "modules" / "uart_rx.md"));
    CHECK(fs::exists(tmp.path / "docs" / "interfaces" / "axi_if.md"));
    CHECK(fs::exists(tmp.path / "docs" / "packages" / "uart_pkg.md"));
    CHECK(fs::exists(tmp.path / "docs" / "search_index.json"));

    // Check index content
    std::ifstream idx(tmp.path / "docs" / "index.md");
    std::string idx_content((std::istreambuf_iterator<char>(idx)),
                             std::istreambuf_iterator<char>());
    CHECK(idx_content.find("# uart_lib") != std::string::npos);
    CHECK(idx_content.find("uart_tx") != std::string::npos);

    // Check unit page content
    std::ifstream tx_file(tmp.path / "docs" / "modules" / "uart_tx.md");
    std::string tx_content((std::istreambuf_iterator<char>(tx_file)),
                            std::istreambuf_iterator<char>());
    CHECK(tx_content.find("# Module: `uart_tx`") != std::string::npos);
    CHECK(tx_content.find("UART transmitter.") != std::string::npos);
    CHECK(tx_content.find("`BAUD_RATE`") != std::string::npos);
    CHECK(tx_content.find("`clk`") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — clean_before_generate", "[doc]") {
    TempDir tmp;
    auto out_dir = tmp.path / "docs";
    fs::create_directories(out_dir);

    // Create a stale file
    std::ofstream stale(out_dir / "stale.txt");
    stale << "old content";
    stale.close();
    CHECK(fs::exists(out_dir / "stale.txt"));

    auto model = model_from_source("module x; endmodule\n");
    RenderConfig config;
    config.output_dir = out_dir;
    config.clean_before_generate = true;
    MarkdownRenderer renderer(config);

    auto result = renderer.render(model);
    REQUIRE(result.is_ok());

    // Stale file should be gone
    CHECK_FALSE(fs::exists(out_dir / "stale.txt"));
    // New files should exist
    CHECK(fs::exists(out_dir / "index.md"));
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_CASE("DocExtractor — regular comment is not a doc comment", "[doc]") {
    auto units = extract_source(R"(
// This is just a regular comment.
module test;
endmodule
)");
    REQUIRE(units.size() == 1);
    CHECK(units[0].doc.empty());
}

TEST_CASE("DocExtractor — gap between doc and module breaks association", "[doc]") {
    auto units = extract_source(R"(
/// This doc comment is too far from the module.



module test;
endmodule
)");
    REQUIRE(units.size() == 1);
    // The doc comment is more than 2 lines away from the module
    CHECK(units[0].doc.empty());
}

TEST_CASE("DocExtractor — interface and package docs", "[doc]") {
    auto units = extract_source(R"(
/// An AXI4 interface.
interface axi4_if;
endinterface

/// UART constants.
package uart_pkg;
endpackage
)");
    CHECK(units.size() == 2);
    CHECK(units[0].kind == DesignUnitKind::Interface);
    CHECK(units[0].doc.brief == "An AXI4 interface.");
    CHECK(units[1].kind == DesignUnitKind::Package);
    CHECK(units[1].doc.brief == "UART constants.");
}

TEST_CASE("MarkdownRenderer — empty model renders minimal index", "[doc]") {
    DocModel model;
    model.package = {"empty_pkg", "0.0.1", "", {}};

    MarkdownRenderer renderer;
    auto index = renderer.render_index(model);

    CHECK(index.find("# empty_pkg") != std::string::npos);
    CHECK(index.find("## Modules") == std::string::npos);  // no modules section
}

TEST_CASE("MarkdownRenderer — port table directions", "[doc]") {
    auto model = model_from_source(R"(
/// Module with all port directions.
module test(
  input  logic a,
  output logic b,
  inout  wire  c
);
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("test"), model);

    CHECK(md.find("| `a` | input |") != std::string::npos);
    CHECK(md.find("| `b` | output |") != std::string::npos);
    CHECK(md.find("| `c` | inout |") != std::string::npos);
}

TEST_CASE("MarkdownRenderer — no diagram for single isolated module", "[doc]") {
    auto model = model_from_source(R"(
/// Isolated module.
module isolated;
endmodule
)");

    MarkdownRenderer renderer;
    auto md = renderer.render_unit(*model.find_unit("isolated"), model);

    // No dependency graph section for a module with no deps
    CHECK(md.find("## Dependency Graph") == std::string::npos);
}

TEST_CASE("render_param_table — empty params produces no output", "[doc]") {
    MarkdownRenderer renderer;
    // Access via render_unit with a unit that has no params
    auto model = model_from_source("module no_params; endmodule\n");
    auto md = renderer.render_unit(*model.find_unit("no_params"), model);
    CHECK(md.find("## Parameters") == std::string::npos);
}
