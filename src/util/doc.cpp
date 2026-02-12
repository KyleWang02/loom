#include <loom/doc.hpp>
#include <loom/lang/parser.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace loom::doc {

// ===========================================================================
// DocComment helpers
// ===========================================================================

bool DocComment::empty() const {
    return brief.empty() && body.empty() && tags.empty();
}

std::vector<DocTag> DocComment::tags_of(DocTagKind kind) const {
    std::vector<DocTag> result;
    for (auto& t : tags) {
        if (t.kind == kind) result.push_back(t);
    }
    return result;
}

std::string DocComment::find_param_doc(const std::string& name) const {
    for (auto& t : tags) {
        if (t.kind == DocTagKind::Param && t.name == name) return t.text;
    }
    return {};
}

std::string DocComment::find_port_doc(const std::string& name) const {
    for (auto& t : tags) {
        if (t.kind == DocTagKind::Port && t.name == name) return t.text;
    }
    return {};
}

bool DocComment::has_deprecated() const {
    for (auto& t : tags) {
        if (t.kind == DocTagKind::Deprecated) return true;
    }
    return false;
}

std::string DocComment::deprecated_message() const {
    for (auto& t : tags) {
        if (t.kind == DocTagKind::Deprecated) return t.text;
    }
    return {};
}

std::vector<std::string> DocComment::see_also() const {
    std::vector<std::string> result;
    for (auto& t : tags) {
        if (t.kind == DocTagKind::See) result.push_back(t.name);
    }
    return result;
}

// ===========================================================================
// Doc comment parsing
// ===========================================================================

// Trim leading/trailing whitespace
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract the first sentence (up to first period followed by space/end, or first newline)
static std::string extract_brief(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            return trim(text.substr(0, i));
        }
        if (text[i] == '.' && (i + 1 >= text.size() || text[i + 1] == ' ' || text[i + 1] == '\n')) {
            return trim(text.substr(0, i + 1));
        }
    }
    return trim(text);
}

// Parse a @tag line. Returns true if it's a tag, populates tag.
static bool parse_tag(const std::string& line, DocTag& tag) {
    if (line.empty() || line[0] != '@') return false;

    // Find tag name end
    auto space = line.find(' ', 1);
    std::string tag_name = (space != std::string::npos)
        ? line.substr(1, space - 1)
        : line.substr(1);
    std::string rest = (space != std::string::npos)
        ? trim(line.substr(space + 1))
        : "";

    if (tag_name == "param") {
        tag.kind = DocTagKind::Param;
        auto name_end = rest.find(' ');
        if (name_end != std::string::npos) {
            tag.name = rest.substr(0, name_end);
            tag.text = trim(rest.substr(name_end + 1));
        } else {
            tag.name = rest;
        }
        return true;
    }
    if (tag_name == "port") {
        tag.kind = DocTagKind::Port;
        auto name_end = rest.find(' ');
        if (name_end != std::string::npos) {
            tag.name = rest.substr(0, name_end);
            tag.text = trim(rest.substr(name_end + 1));
        } else {
            tag.name = rest;
        }
        return true;
    }
    if (tag_name == "see") {
        tag.kind = DocTagKind::See;
        tag.name = rest;
        return true;
    }
    if (tag_name == "deprecated") {
        tag.kind = DocTagKind::Deprecated;
        tag.text = rest;
        return true;
    }
    if (tag_name == "example") {
        tag.kind = DocTagKind::Example;
        tag.text = rest;
        return true;
    }
    if (tag_name == "note") {
        tag.kind = DocTagKind::Note;
        tag.text = rest;
        return true;
    }
    if (tag_name == "warning") {
        tag.kind = DocTagKind::Warning;
        tag.text = rest;
        return true;
    }
    if (tag_name == "wavedrom") {
        tag.kind = DocTagKind::WaveDrom;
        tag.text = rest;
        return true;
    }
    return false;
}

DocComment parse_doc_comment(const std::string& raw_text, int start_line) {
    DocComment doc;
    doc.line = start_line;

    if (raw_text.empty()) return doc;

    // Split into lines
    std::vector<std::string> lines;
    std::istringstream iss(raw_text);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(trim(line));
    }

    // Separate body text from tags
    std::string body_text;
    DocTag current_tag;
    bool in_tag = false;

    for (auto& l : lines) {
        DocTag tag;
        if (parse_tag(l, tag)) {
            // Flush previous tag if any
            if (in_tag) {
                doc.tags.push_back(current_tag);
            }
            current_tag = tag;
            in_tag = true;
        } else if (in_tag) {
            // Continuation of previous tag
            if (!l.empty()) {
                if (!current_tag.text.empty()) current_tag.text += "\n";
                current_tag.text += l;
            }
        } else {
            // Body text (before any tags)
            if (!body_text.empty()) body_text += "\n";
            body_text += l;
        }
    }
    if (in_tag) {
        doc.tags.push_back(current_tag);
    }

    body_text = trim(body_text);
    doc.body = body_text;
    doc.brief = extract_brief(body_text);

    return doc;
}

// ===========================================================================
// DocExtractor
// ===========================================================================

DocExtractor::DocExtractor(DocExtractorConfig config)
    : config_(config) {}

DocComment DocExtractor::find_leading_doc(const std::vector<Comment>& comments,
                                           int unit_line) {
    // Collect all DocLine comments
    std::vector<const Comment*> doc_comments;
    for (auto& c : comments) {
        if (c.kind == CommentKind::DocLine) {
            doc_comments.push_back(&c);
        }
    }

    if (doc_comments.empty()) return {};

    // Find consecutive DocLine comments ending just before unit_line
    // Walk backwards from comments that could be before unit_line
    std::vector<const Comment*> block;
    for (int i = static_cast<int>(doc_comments.size()) - 1; i >= 0; --i) {
        auto* c = doc_comments[i];
        if (c->pos.line >= unit_line) continue;

        // This comment is before the unit. Check if it's adjacent.
        if (block.empty()) {
            // First comment found (closest to unit_line)
            // Must be within 1 line of the unit
            if (unit_line - c->pos.line <= 2) {
                block.push_back(c);
            } else {
                break;
            }
        } else {
            // Check if consecutive with previous block member
            int prev_line = block.back()->pos.line;
            if (prev_line - c->pos.line == 1) {
                block.push_back(c);
            } else {
                break;
            }
        }
    }

    if (block.empty()) return {};

    // Reverse to get chronological order
    std::reverse(block.begin(), block.end());

    // Concatenate text
    std::string raw;
    for (auto* c : block) {
        if (!raw.empty()) raw += "\n";
        raw += c->text;
    }

    return parse_doc_comment(raw, block.front()->pos.line);
}

std::string DocExtractor::find_trailing_doc(const std::vector<Comment>& comments,
                                             int line) {
    for (auto& c : comments) {
        if (c.kind == CommentKind::DocLine && c.pos.line == line) {
            return trim(c.text);
        }
    }
    return {};
}

std::vector<DesignUnitDoc> DocExtractor::extract(const LexResult& lex,
                                                  const ParseResult& parse,
                                                  const std::string& filename) {
    std::vector<DesignUnitDoc> result;

    for (auto& unit : parse.units) {
        if (unit.depth != 0) continue;  // only top-level units

        DesignUnitDoc doc_unit;
        doc_unit.kind = unit.kind;
        doc_unit.name = unit.name;
        doc_unit.file = filename;
        doc_unit.line = unit.pos.line;

        // Find leading doc comment
        doc_unit.doc = find_leading_doc(lex.comments, unit.pos.line);

        // Build port docs
        for (auto& port : unit.ports) {
            PortDoc pd;
            pd.name = port.name;
            pd.direction = port.direction;
            pd.type_text = port.type_text;
            // Priority: @port tag > trailing comment
            pd.description = doc_unit.doc.find_port_doc(port.name);
            if (pd.description.empty()) {
                pd.description = find_trailing_doc(lex.comments, port.pos.line);
            }
            // Also check @param for ports (common in practice)
            if (pd.description.empty()) {
                pd.description = doc_unit.doc.find_param_doc(port.name);
            }
            doc_unit.ports.push_back(std::move(pd));
        }

        // Build param docs
        for (auto& param : unit.params) {
            ParamDoc pd;
            pd.name = param.name;
            pd.default_text = param.default_text;
            pd.is_localparam = param.is_localparam;
            pd.description = doc_unit.doc.find_param_doc(param.name);
            if (pd.description.empty()) {
                pd.description = find_trailing_doc(lex.comments, param.pos.line);
            }
            doc_unit.params.push_back(std::move(pd));
        }

        // Build instantiation list
        for (auto& inst : unit.instantiations) {
            // Avoid duplicates
            if (std::find(doc_unit.instantiates.begin(), doc_unit.instantiates.end(),
                          inst.module_name) == doc_unit.instantiates.end()) {
                doc_unit.instantiates.push_back(inst.module_name);
            }
        }

        result.push_back(std::move(doc_unit));
    }

    return result;
}

DocModel DocExtractor::extract_all(
    const std::vector<std::tuple<std::string, LexResult, ParseResult>>& files,
    const PackageInfo& pkg) {

    DocModel model;
    model.package = pkg;

    for (auto& [filename, lex, parse] : files) {
        auto units = extract(lex, parse, filename);
        for (auto& u : units) {
            model.units.push_back(std::move(u));
        }
    }

    model.resolve_cross_refs();
    return model;
}

// ===========================================================================
// DocModel
// ===========================================================================

void DocModel::resolve_cross_refs() {
    // Build name -> index map
    std::unordered_map<std::string, size_t> name_to_idx;
    for (size_t i = 0; i < units.size(); ++i) {
        name_to_idx[units[i].name] = i;
    }

    // For each unit, populate instantiated_by
    for (auto& unit : units) {
        for (auto& inst_name : unit.instantiates) {
            auto it = name_to_idx.find(inst_name);
            if (it != name_to_idx.end()) {
                auto& target = units[it->second];
                if (std::find(target.instantiated_by.begin(),
                              target.instantiated_by.end(),
                              unit.name) == target.instantiated_by.end()) {
                    target.instantiated_by.push_back(unit.name);
                }
            }
        }
    }
}

const DesignUnitDoc* DocModel::find_unit(const std::string& name) const {
    for (auto& u : units) {
        if (u.name == name) return &u;
    }
    return nullptr;
}

std::vector<const DesignUnitDoc*> DocModel::modules() const {
    std::vector<const DesignUnitDoc*> result;
    for (auto& u : units) {
        if (u.kind == DesignUnitKind::Module) result.push_back(&u);
    }
    return result;
}

std::vector<const DesignUnitDoc*> DocModel::interfaces() const {
    std::vector<const DesignUnitDoc*> result;
    for (auto& u : units) {
        if (u.kind == DesignUnitKind::Interface) result.push_back(&u);
    }
    return result;
}

std::vector<const DesignUnitDoc*> DocModel::packages() const {
    std::vector<const DesignUnitDoc*> result;
    for (auto& u : units) {
        if (u.kind == DesignUnitKind::Package) result.push_back(&u);
    }
    return result;
}

// ===========================================================================
// MarkdownRenderer
// ===========================================================================

MarkdownRenderer::MarkdownRenderer(RenderConfig config)
    : config_(std::move(config)) {}

std::string MarkdownRenderer::kind_name(DesignUnitKind kind) const {
    switch (kind) {
        case DesignUnitKind::Module:    return "Module";
        case DesignUnitKind::Package:   return "Package";
        case DesignUnitKind::Interface: return "Interface";
        case DesignUnitKind::Class:     return "Class";
        case DesignUnitKind::Program:   return "Program";
    }
    return "Unknown";
}

std::string MarkdownRenderer::kind_dir(DesignUnitKind kind) const {
    switch (kind) {
        case DesignUnitKind::Module:    return "modules";
        case DesignUnitKind::Package:   return "packages";
        case DesignUnitKind::Interface: return "interfaces";
        case DesignUnitKind::Class:     return "classes";
        case DesignUnitKind::Program:   return "programs";
    }
    return "other";
}

std::string MarkdownRenderer::render_param_table(const std::vector<ParamDoc>& params) {
    if (params.empty()) return {};
    std::ostringstream ss;
    ss << "## Parameters\n\n";
    ss << "| Name | Default | Description |\n";
    ss << "|------|---------|-------------|\n";
    for (auto& p : params) {
        ss << "| `" << p.name << "` | "
           << (p.default_text.empty() ? "-" : "`" + p.default_text + "`") << " | "
           << (p.description.empty() ? "-" : p.description) << " |\n";
    }
    ss << "\n";
    return ss.str();
}

std::string MarkdownRenderer::render_port_table(const std::vector<PortDoc>& ports) {
    if (ports.empty()) return {};
    std::ostringstream ss;
    ss << "## Ports\n\n";
    ss << "| Name | Direction | Type | Description |\n";
    ss << "|------|-----------|------|-------------|\n";
    for (auto& p : ports) {
        std::string dir;
        switch (p.direction) {
            case PortDirection::Input:  dir = "input"; break;
            case PortDirection::Output: dir = "output"; break;
            case PortDirection::Inout:  dir = "inout"; break;
            case PortDirection::Ref:    dir = "ref"; break;
        }
        ss << "| `" << p.name << "` | " << dir << " | "
           << (p.type_text.empty() ? "-" : "`" + p.type_text + "`") << " | "
           << (p.description.empty() ? "-" : p.description) << " |\n";
    }
    ss << "\n";
    return ss.str();
}

std::string MarkdownRenderer::render_index(const DocModel& model) {
    std::ostringstream ss;

    ss << "# " << model.package.name << "\n\n";
    if (!model.package.version.empty()) {
        ss << "**Version**: " << model.package.version << "\n\n";
    }

    // Modules table
    auto mods = model.modules();
    if (!mods.empty()) {
        ss << "## Modules\n\n";
        ss << "| Module | Description |\n";
        ss << "|--------|-------------|\n";
        for (auto* m : mods) {
            std::string brief = m->doc.brief.empty() ? "-" : m->doc.brief;
            ss << "| [`" << m->name << "`](modules/" << m->name << ".md) | "
               << brief << " |\n";
        }
        ss << "\n";
    }

    // Interfaces table
    auto ifs = model.interfaces();
    if (!ifs.empty()) {
        ss << "## Interfaces\n\n";
        ss << "| Interface | Description |\n";
        ss << "|-----------|-------------|\n";
        for (auto* m : ifs) {
            std::string brief = m->doc.brief.empty() ? "-" : m->doc.brief;
            ss << "| [`" << m->name << "`](interfaces/" << m->name << ".md) | "
               << brief << " |\n";
        }
        ss << "\n";
    }

    // Packages table
    auto pkgs = model.packages();
    if (!pkgs.empty()) {
        ss << "## Packages\n\n";
        ss << "| Package | Description |\n";
        ss << "|---------|-------------|\n";
        for (auto* m : pkgs) {
            std::string brief = m->doc.brief.empty() ? "-" : m->doc.brief;
            ss << "| [`" << m->name << "`](packages/" << m->name << ".md) | "
               << brief << " |\n";
        }
        ss << "\n";
    }

    // Project dependency graph
    if (config_.generate_diagrams && model.units.size() > 1) {
        ss << render_mermaid_graph(model);
    }

    return ss.str();
}

std::string MarkdownRenderer::render_unit(const DesignUnitDoc& unit,
                                           const DocModel& model) {
    std::ostringstream ss;

    ss << "# " << kind_name(unit.kind) << ": `" << unit.name << "`\n\n";

    // Deprecation notice
    if (unit.doc.has_deprecated()) {
        ss << "> **Deprecated**";
        auto msg = unit.doc.deprecated_message();
        if (!msg.empty()) ss << ": " << msg;
        ss << "\n\n";
    }

    // Source location
    ss << "**Source**: `" << unit.file << ":" << unit.line << "`\n\n";

    // Description
    if (!unit.doc.body.empty()) {
        ss << unit.doc.body << "\n\n";
    }

    // Parameters
    ss << render_param_table(unit.params);

    // Ports
    ss << render_port_table(unit.ports);

    // Dependencies
    if (!unit.instantiates.empty()) {
        ss << "## Instantiates\n\n";
        for (auto& name : unit.instantiates) {
            auto* target = model.find_unit(name);
            if (target) {
                ss << "- [`" << name << "`](../" << kind_dir(target->kind)
                   << "/" << name << ".md)\n";
            } else {
                ss << "- `" << name << "` (external)\n";
            }
        }
        ss << "\n";
    }

    if (!unit.instantiated_by.empty()) {
        ss << "## Instantiated By\n\n";
        for (auto& name : unit.instantiated_by) {
            auto* source = model.find_unit(name);
            if (source) {
                ss << "- [`" << name << "`](../" << kind_dir(source->kind)
                   << "/" << name << ".md)\n";
            } else {
                ss << "- `" << name << "`\n";
            }
        }
        ss << "\n";
    }

    // See also
    auto sees = unit.doc.see_also();
    if (!sees.empty()) {
        ss << "## See Also\n\n";
        for (auto& name : sees) {
            auto* target = model.find_unit(name);
            if (target) {
                ss << "- [`" << name << "`](../" << kind_dir(target->kind)
                   << "/" << name << ".md)\n";
            } else {
                ss << "- `" << name << "`\n";
            }
        }
        ss << "\n";
    }

    // Unit-level dependency graph
    if (config_.generate_diagrams &&
        (!unit.instantiates.empty() || !unit.instantiated_by.empty())) {
        ss << render_unit_graph(unit, model);
    }

    // Examples
    auto examples = unit.doc.tags_of(DocTagKind::Example);
    if (!examples.empty()) {
        ss << "## Examples\n\n";
        for (auto& ex : examples) {
            ss << "```systemverilog\n" << ex.text << "\n```\n\n";
        }
    }

    // Notes
    auto notes = unit.doc.tags_of(DocTagKind::Note);
    for (auto& n : notes) {
        ss << "> **Note**: " << n.text << "\n\n";
    }

    // Warnings
    auto warnings = unit.doc.tags_of(DocTagKind::Warning);
    for (auto& w : warnings) {
        ss << "> **Warning**: " << w.text << "\n\n";
    }

    return ss.str();
}

std::string MarkdownRenderer::render_mermaid_graph(const DocModel& model) {
    std::ostringstream ss;
    ss << "## Dependency Graph\n\n";
    ss << "```mermaid\ngraph TD\n";

    for (auto& unit : model.units) {
        ss << "    " << unit.name << "[\"" << unit.name << "\"]\n";
    }
    for (auto& unit : model.units) {
        for (auto& inst : unit.instantiates) {
            ss << "    " << unit.name << " --> " << inst << "\n";
        }
    }

    ss << "```\n\n";
    return ss.str();
}

std::string MarkdownRenderer::render_unit_graph(const DesignUnitDoc& unit,
                                                 const DocModel& model) {
    std::ostringstream ss;
    ss << "## Dependency Graph\n\n";
    ss << "```mermaid\ngraph TD\n";

    // Highlight current unit
    ss << "    " << unit.name << "[\"" << unit.name << "\"]:::" << "current\n";

    // Parents (instantiated_by)
    for (auto& name : unit.instantiated_by) {
        ss << "    " << name << "[\"" << name << "\"]\n";
        ss << "    " << name << " --> " << unit.name << "\n";
    }

    // Children (instantiates)
    for (auto& name : unit.instantiates) {
        ss << "    " << name << "[\"" << name << "\"]\n";
        ss << "    " << unit.name << " --> " << name << "\n";
    }

    ss << "    classDef current fill:#f9f,stroke:#333,stroke-width:2px\n";
    ss << "```\n\n";
    return ss.str();
}

std::string MarkdownRenderer::render_search_index(const DocModel& model) {
    std::ostringstream ss;
    ss << "{\"units\":[";

    for (size_t i = 0; i < model.units.size(); ++i) {
        auto& u = model.units[i];
        if (i > 0) ss << ",";
        ss << "{\"name\":\"" << u.name
           << "\",\"kind\":\"" << kind_name(u.kind)
           << "\",\"brief\":\"" << u.doc.brief
           << "\",\"file\":\"" << u.file
           << "\",\"path\":\"" << kind_dir(u.kind) << "/" << u.name << ".md"
           << "\"}";
    }

    ss << "],\"package\":{\"name\":\"" << model.package.name
       << "\",\"version\":\"" << model.package.version << "\"}}";
    return ss.str();
}

Result<std::monostate> MarkdownRenderer::render(const DocModel& model) {
    auto base = config_.output_dir;

    // Clean if requested
    if (config_.clean_before_generate && fs::exists(base)) {
        fs::remove_all(base);
    }

    // Create directories
    fs::create_directories(base / "modules");
    fs::create_directories(base / "interfaces");
    fs::create_directories(base / "packages");

    // Write index
    {
        std::ofstream f(base / "index.md");
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write " + (base / "index.md").string());
        }
        f << render_index(model);
    }

    // Write per-unit pages
    for (auto& unit : model.units) {
        auto dir = base / kind_dir(unit.kind);
        fs::create_directories(dir);
        auto path = dir / (unit.name + ".md");
        std::ofstream f(path);
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write " + path.string());
        }
        f << render_unit(unit, model);
    }

    // Write search index
    {
        std::ofstream f(base / "search_index.json");
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write search_index.json");
        }
        f << render_search_index(model);
    }

    return ok_status();
}

// ===========================================================================
// HtmlRenderer
// ===========================================================================

static const char* CSS_STYLE = R"CSS(
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
       max-width: 900px; margin: 0 auto; padding: 20px; line-height: 1.6; color: #333; }
h1 { border-bottom: 2px solid #eee; padding-bottom: 10px; }
h2 { border-bottom: 1px solid #eee; padding-bottom: 5px; margin-top: 30px; }
table { border-collapse: collapse; width: 100%; margin: 10px 0; }
th, td { border: 1px solid #ddd; padding: 8px 12px; text-align: left; }
th { background: #f5f5f5; font-weight: 600; }
tr:nth-child(even) { background: #fafafa; }
code { background: #f0f0f0; padding: 2px 6px; border-radius: 3px; font-size: 0.9em; }
pre { background: #f5f5f5; padding: 15px; border-radius: 5px; overflow-x: auto; }
pre code { background: none; padding: 0; }
a { color: #0366d6; text-decoration: none; }
a:hover { text-decoration: underline; }
.deprecated { background: #fff3cd; border: 1px solid #ffc107; padding: 10px; border-radius: 5px; margin: 10px 0; }
.note { background: #d1ecf1; border: 1px solid #bee5eb; padding: 10px; border-radius: 5px; margin: 10px 0; }
.warning { background: #f8d7da; border: 1px solid #f5c6cb; padding: 10px; border-radius: 5px; margin: 10px 0; }
.source-loc { color: #666; font-size: 0.9em; }
nav { margin-bottom: 20px; }
nav a { margin-right: 15px; }
)CSS";

HtmlRenderer::HtmlRenderer(RenderConfig config)
    : config_(std::move(config)) {}

std::string HtmlRenderer::html_escape(const std::string& text) const {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:   result += c;
        }
    }
    return result;
}

std::string HtmlRenderer::kind_name(DesignUnitKind kind) const {
    switch (kind) {
        case DesignUnitKind::Module:    return "Module";
        case DesignUnitKind::Package:   return "Package";
        case DesignUnitKind::Interface: return "Interface";
        case DesignUnitKind::Class:     return "Class";
        case DesignUnitKind::Program:   return "Program";
    }
    return "Unknown";
}

std::string HtmlRenderer::kind_dir(DesignUnitKind kind) const {
    switch (kind) {
        case DesignUnitKind::Module:    return "modules";
        case DesignUnitKind::Package:   return "packages";
        case DesignUnitKind::Interface: return "interfaces";
        case DesignUnitKind::Class:     return "classes";
        case DesignUnitKind::Program:   return "programs";
    }
    return "other";
}

std::string HtmlRenderer::html_head(const std::string& title) const {
    std::ostringstream ss;
    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"UTF-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
       << "<title>" << html_escape(title) << "</title>\n"
       << "<style>" << CSS_STYLE << "</style>\n"
       << "</head>\n<body>\n";
    return ss.str();
}

std::string HtmlRenderer::html_footer() const {
    return "<script src=\"https://cdn.jsdelivr.net/npm/mermaid/dist/mermaid.min.js\"></script>\n"
           "<script>mermaid.initialize({startOnLoad:true});</script>\n"
           "</body>\n</html>\n";
}

std::string HtmlRenderer::render_param_table(const std::vector<ParamDoc>& params) {
    if (params.empty()) return {};
    std::ostringstream ss;
    ss << "<h2>Parameters</h2>\n<table>\n"
       << "<tr><th>Name</th><th>Default</th><th>Description</th></tr>\n";
    for (auto& p : params) {
        ss << "<tr><td><code>" << html_escape(p.name) << "</code></td><td>"
           << (p.default_text.empty() ? "-" : "<code>" + html_escape(p.default_text) + "</code>")
           << "</td><td>"
           << (p.description.empty() ? "-" : html_escape(p.description))
           << "</td></tr>\n";
    }
    ss << "</table>\n";
    return ss.str();
}

std::string HtmlRenderer::render_port_table(const std::vector<PortDoc>& ports) {
    if (ports.empty()) return {};
    std::ostringstream ss;
    ss << "<h2>Ports</h2>\n<table>\n"
       << "<tr><th>Name</th><th>Direction</th><th>Type</th><th>Description</th></tr>\n";
    for (auto& p : ports) {
        std::string dir;
        switch (p.direction) {
            case PortDirection::Input:  dir = "input"; break;
            case PortDirection::Output: dir = "output"; break;
            case PortDirection::Inout:  dir = "inout"; break;
            case PortDirection::Ref:    dir = "ref"; break;
        }
        ss << "<tr><td><code>" << html_escape(p.name) << "</code></td><td>"
           << dir << "</td><td>"
           << (p.type_text.empty() ? "-" : "<code>" + html_escape(p.type_text) + "</code>")
           << "</td><td>"
           << (p.description.empty() ? "-" : html_escape(p.description))
           << "</td></tr>\n";
    }
    ss << "</table>\n";
    return ss.str();
}

std::string HtmlRenderer::render_mermaid_graph(const DocModel& model) {
    std::ostringstream ss;
    ss << "<h2>Dependency Graph</h2>\n"
       << "<div class=\"mermaid\">\ngraph TD\n";
    for (auto& unit : model.units) {
        ss << "    " << unit.name << "[\"" << html_escape(unit.name) << "\"]\n";
    }
    for (auto& unit : model.units) {
        for (auto& inst : unit.instantiates) {
            ss << "    " << unit.name << " --> " << inst << "\n";
        }
    }
    ss << "</div>\n";
    return ss.str();
}

std::string HtmlRenderer::render_unit_graph(const DesignUnitDoc& unit,
                                             const DocModel& /*model*/) {
    std::ostringstream ss;
    ss << "<h2>Dependency Graph</h2>\n"
       << "<div class=\"mermaid\">\ngraph TD\n";
    ss << "    " << unit.name << "[\"" << html_escape(unit.name) << "\"]:::current\n";
    for (auto& name : unit.instantiated_by) {
        ss << "    " << name << "[\"" << html_escape(name) << "\"]\n";
        ss << "    " << name << " --> " << unit.name << "\n";
    }
    for (auto& name : unit.instantiates) {
        ss << "    " << name << "[\"" << html_escape(name) << "\"]\n";
        ss << "    " << unit.name << " --> " << name << "\n";
    }
    ss << "    classDef current fill:#f9f,stroke:#333,stroke-width:2px\n";
    ss << "</div>\n";
    return ss.str();
}

std::string HtmlRenderer::render_index(const DocModel& model) {
    std::ostringstream ss;
    ss << html_head(model.package.name + " Documentation");
    ss << "<h1>" << html_escape(model.package.name) << "</h1>\n";
    if (!model.package.version.empty()) {
        ss << "<p><strong>Version</strong>: " << html_escape(model.package.version) << "</p>\n";
    }

    auto mods = model.modules();
    if (!mods.empty()) {
        ss << "<h2>Modules</h2>\n<table>\n"
           << "<tr><th>Module</th><th>Description</th></tr>\n";
        for (auto* m : mods) {
            ss << "<tr><td><a href=\"modules/" << m->name << ".html\"><code>"
               << html_escape(m->name) << "</code></a></td><td>"
               << (m->doc.brief.empty() ? "-" : html_escape(m->doc.brief))
               << "</td></tr>\n";
        }
        ss << "</table>\n";
    }

    auto ifs = model.interfaces();
    if (!ifs.empty()) {
        ss << "<h2>Interfaces</h2>\n<table>\n"
           << "<tr><th>Interface</th><th>Description</th></tr>\n";
        for (auto* m : ifs) {
            ss << "<tr><td><a href=\"interfaces/" << m->name << ".html\"><code>"
               << html_escape(m->name) << "</code></a></td><td>"
               << (m->doc.brief.empty() ? "-" : html_escape(m->doc.brief))
               << "</td></tr>\n";
        }
        ss << "</table>\n";
    }

    auto pkgs = model.packages();
    if (!pkgs.empty()) {
        ss << "<h2>Packages</h2>\n<table>\n"
           << "<tr><th>Package</th><th>Description</th></tr>\n";
        for (auto* m : pkgs) {
            ss << "<tr><td><a href=\"packages/" << m->name << ".html\"><code>"
               << html_escape(m->name) << "</code></a></td><td>"
               << (m->doc.brief.empty() ? "-" : html_escape(m->doc.brief))
               << "</td></tr>\n";
        }
        ss << "</table>\n";
    }

    if (config_.generate_diagrams && model.units.size() > 1) {
        ss << render_mermaid_graph(model);
    }

    ss << html_footer();
    return ss.str();
}

std::string HtmlRenderer::render_unit(const DesignUnitDoc& unit,
                                       const DocModel& model) {
    std::ostringstream ss;
    ss << html_head(kind_name(unit.kind) + ": " + unit.name);
    ss << "<nav><a href=\"../index.html\">Index</a></nav>\n";
    ss << "<h1>" << kind_name(unit.kind) << ": <code>"
       << html_escape(unit.name) << "</code></h1>\n";

    if (unit.doc.has_deprecated()) {
        ss << "<div class=\"deprecated\"><strong>Deprecated</strong>";
        auto msg = unit.doc.deprecated_message();
        if (!msg.empty()) ss << ": " << html_escape(msg);
        ss << "</div>\n";
    }

    ss << "<p class=\"source-loc\"><strong>Source</strong>: <code>"
       << html_escape(unit.file) << ":" << unit.line << "</code></p>\n";

    if (!unit.doc.body.empty()) {
        ss << "<p>" << html_escape(unit.doc.body) << "</p>\n";
    }

    ss << render_param_table(unit.params);
    ss << render_port_table(unit.ports);

    if (!unit.instantiates.empty()) {
        ss << "<h2>Instantiates</h2>\n<ul>\n";
        for (auto& name : unit.instantiates) {
            auto* target = model.find_unit(name);
            if (target) {
                ss << "<li><a href=\"../" << kind_dir(target->kind)
                   << "/" << name << ".html\"><code>" << html_escape(name)
                   << "</code></a></li>\n";
            } else {
                ss << "<li><code>" << html_escape(name) << "</code> (external)</li>\n";
            }
        }
        ss << "</ul>\n";
    }

    if (!unit.instantiated_by.empty()) {
        ss << "<h2>Instantiated By</h2>\n<ul>\n";
        for (auto& name : unit.instantiated_by) {
            auto* source = model.find_unit(name);
            if (source) {
                ss << "<li><a href=\"../" << kind_dir(source->kind)
                   << "/" << name << ".html\"><code>" << html_escape(name)
                   << "</code></a></li>\n";
            } else {
                ss << "<li><code>" << html_escape(name) << "</code></li>\n";
            }
        }
        ss << "</ul>\n";
    }

    auto sees = unit.doc.see_also();
    if (!sees.empty()) {
        ss << "<h2>See Also</h2>\n<ul>\n";
        for (auto& name : sees) {
            auto* target = model.find_unit(name);
            if (target) {
                ss << "<li><a href=\"../" << kind_dir(target->kind)
                   << "/" << name << ".html\"><code>" << html_escape(name)
                   << "</code></a></li>\n";
            } else {
                ss << "<li><code>" << html_escape(name) << "</code></li>\n";
            }
        }
        ss << "</ul>\n";
    }

    if (config_.generate_diagrams &&
        (!unit.instantiates.empty() || !unit.instantiated_by.empty())) {
        ss << render_unit_graph(unit, model);
    }

    auto examples = unit.doc.tags_of(DocTagKind::Example);
    if (!examples.empty()) {
        ss << "<h2>Examples</h2>\n";
        for (auto& ex : examples) {
            ss << "<pre><code>" << html_escape(ex.text) << "</code></pre>\n";
        }
    }

    auto notes = unit.doc.tags_of(DocTagKind::Note);
    for (auto& n : notes) {
        ss << "<div class=\"note\"><strong>Note</strong>: "
           << html_escape(n.text) << "</div>\n";
    }

    auto warnings = unit.doc.tags_of(DocTagKind::Warning);
    for (auto& w : warnings) {
        ss << "<div class=\"warning\"><strong>Warning</strong>: "
           << html_escape(w.text) << "</div>\n";
    }

    ss << html_footer();
    return ss.str();
}

std::string HtmlRenderer::render_search_index(const DocModel& model) {
    std::ostringstream ss;
    ss << "{\"units\":[";
    for (size_t i = 0; i < model.units.size(); ++i) {
        auto& u = model.units[i];
        if (i > 0) ss << ",";
        ss << "{\"name\":\"" << u.name
           << "\",\"kind\":\"" << kind_name(u.kind)
           << "\",\"brief\":\"" << u.doc.brief
           << "\",\"file\":\"" << u.file
           << "\",\"path\":\"" << kind_dir(u.kind) << "/" << u.name << ".html"
           << "\"}";
    }
    ss << "],\"package\":{\"name\":\"" << model.package.name
       << "\",\"version\":\"" << model.package.version << "\"}}";
    return ss.str();
}

Result<std::monostate> HtmlRenderer::render(const DocModel& model) {
    auto base = config_.output_dir;

    if (config_.clean_before_generate && fs::exists(base)) {
        fs::remove_all(base);
    }

    fs::create_directories(base / "modules");
    fs::create_directories(base / "interfaces");
    fs::create_directories(base / "packages");

    // Write index
    {
        std::ofstream f(base / "index.html");
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write " + (base / "index.html").string());
        }
        f << render_index(model);
    }

    // Write per-unit pages
    for (auto& unit : model.units) {
        auto dir = base / kind_dir(unit.kind);
        fs::create_directories(dir);
        auto path = dir / (unit.name + ".html");
        std::ofstream f(path);
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write " + path.string());
        }
        f << render_unit(unit, model);
    }

    // Write search index
    {
        std::ofstream f(base / "search_index.json");
        if (!f.is_open()) {
            return LoomError(LoomError::IO, "cannot write search_index.json");
        }
        f << render_search_index(model);
    }

    return ok_status();
}

} // namespace loom::doc
