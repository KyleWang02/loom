#pragma once

#include <loom/lang/ir.hpp>
#include <loom/lang/lexer.hpp>
#include <loom/manifest.hpp>
#include <loom/result.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace loom::doc {

// ---------------------------------------------------------------------------
// Doc comment tag types
// ---------------------------------------------------------------------------

enum class DocTagKind {
    Param,       // @param NAME description
    Port,        // @port NAME description
    See,         // @see module_name
    Deprecated,  // @deprecated [message]
    Example,     // @example ... (until next tag or end)
    Note,        // @note text
    Warning,     // @warning text
    WaveDrom     // @wavedrom JSON
};

struct DocTag {
    DocTagKind kind;
    std::string name;  // for Param/Port: the identifier; for See: the target
    std::string text;  // tag content
};

// ---------------------------------------------------------------------------
// DocComment — parsed documentation from /// comments
// ---------------------------------------------------------------------------

struct DocComment {
    std::string brief;              // one-line summary
    std::string body;               // full description (Markdown)
    std::vector<DocTag> tags;
    int line = 0;                   // first line of the doc block

    // Query helpers
    std::vector<DocTag> tags_of(DocTagKind kind) const;
    std::string find_param_doc(const std::string& name) const;
    std::string find_port_doc(const std::string& name) const;
    bool has_deprecated() const;
    std::string deprecated_message() const;
    std::vector<std::string> see_also() const;
    bool empty() const;
};

// Parse a raw doc block (lines without the /// prefix) into a DocComment.
DocComment parse_doc_comment(const std::string& raw_text, int start_line = 0);

// ---------------------------------------------------------------------------
// Documentation model types
// ---------------------------------------------------------------------------

struct PortDoc {
    std::string name;
    PortDirection direction = PortDirection::Input;
    std::string type_text;
    std::string description;
};

struct ParamDoc {
    std::string name;
    std::string default_text;
    bool is_localparam = false;
    std::string description;
};

struct DesignUnitDoc {
    DesignUnitKind kind = DesignUnitKind::Module;
    std::string name;
    std::string file;
    int line = 0;
    DocComment doc;
    std::vector<PortDoc> ports;
    std::vector<ParamDoc> params;
    std::vector<std::string> instantiates;      // modules this unit uses
    std::vector<std::string> instantiated_by;   // modules that use this unit
};

// ---------------------------------------------------------------------------
// DocModel — intermediate representation for the whole project
// ---------------------------------------------------------------------------

struct DocModel {
    PackageInfo package;
    std::vector<DesignUnitDoc> units;

    // Populate instantiated_by from instantiates relationships
    void resolve_cross_refs();

    // Find a unit by name (nullptr if not found)
    const DesignUnitDoc* find_unit(const std::string& name) const;

    // Categorized access
    std::vector<const DesignUnitDoc*> modules() const;
    std::vector<const DesignUnitDoc*> interfaces() const;
    std::vector<const DesignUnitDoc*> packages() const;
};

// ---------------------------------------------------------------------------
// DocExtractor — extract documentation from parsed sources
// ---------------------------------------------------------------------------

struct DocExtractorConfig {
    bool include_private = false;
    bool auto_brief = true;
    bool require_doc_comments = false;
};

class DocExtractor {
public:
    explicit DocExtractor(DocExtractorConfig config = {});

    // Extract from a single file's lex + parse results
    std::vector<DesignUnitDoc> extract(const LexResult& lex,
                                       const ParseResult& parse,
                                       const std::string& filename);

    // Build a complete DocModel from multiple files
    DocModel extract_all(
        const std::vector<std::tuple<std::string, LexResult, ParseResult>>& files,
        const PackageInfo& pkg);

private:
    DocExtractorConfig config_;

    // Find the doc block (consecutive /// comments) ending just before a line
    DocComment find_leading_doc(const std::vector<Comment>& comments, int unit_line);

    // Find trailing /// comment on a specific line
    std::string find_trailing_doc(const std::vector<Comment>& comments, int line);
};

// ---------------------------------------------------------------------------
// MarkdownRenderer — render DocModel to Markdown files
// ---------------------------------------------------------------------------

struct RenderConfig {
    std::filesystem::path output_dir = "docs/api";
    bool generate_diagrams = true;
    bool clean_before_generate = false;
};

class MarkdownRenderer {
public:
    explicit MarkdownRenderer(RenderConfig config = {});

    // Render the full documentation to disk
    Result<std::monostate> render(const DocModel& model);

    // Individual rendering (testable, return string content)
    std::string render_index(const DocModel& model);
    std::string render_unit(const DesignUnitDoc& unit, const DocModel& model);
    std::string render_mermaid_graph(const DocModel& model);
    std::string render_unit_graph(const DesignUnitDoc& unit, const DocModel& model);
    std::string render_search_index(const DocModel& model);

private:
    RenderConfig config_;

    std::string render_param_table(const std::vector<ParamDoc>& params);
    std::string render_port_table(const std::vector<PortDoc>& ports);
    std::string kind_name(DesignUnitKind kind) const;
    std::string kind_dir(DesignUnitKind kind) const;
};

} // namespace loom::doc
