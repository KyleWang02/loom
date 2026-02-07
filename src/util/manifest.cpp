#include <loom/manifest.hpp>
#include <tomlplusplus/toml.hpp>
#include <fstream>
#include <sstream>

namespace loom {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Result<Dependency> parse_dependency(const std::string& name,
                                            const toml::node& node) {
    Dependency dep;
    dep.name = name;

    if (!node.is_table()) {
        return LoomError{LoomError::Manifest,
            "dependency '" + name + "' must be a table"};
    }
    const auto& tbl = *node.as_table();

    // workspace = true
    if (auto ws = tbl["workspace"].value<bool>()) {
        dep.workspace = *ws;
    }

    // member = true
    if (auto m = tbl["member"].value<bool>()) {
        dep.member = *m;
    }

    // git source
    if (auto url = tbl["git"].value<std::string>()) {
        GitSource gs;
        gs.url = *url;
        if (auto v = tbl["tag"].value<std::string>()) gs.tag = *v;
        if (auto v = tbl["version"].value<std::string>()) gs.version = *v;
        if (auto v = tbl["rev"].value<std::string>()) gs.rev = *v;
        if (auto v = tbl["branch"].value<std::string>()) gs.branch = *v;
        dep.git = std::move(gs);
    }

    // path source
    if (auto p = tbl["path"].value<std::string>()) {
        dep.path = PathSource{std::string(*p)};
    }

    auto status = dep.validate();
    if (status.is_err()) return std::move(status).error();

    return Result<Dependency>::ok(std::move(dep));
}

static Result<std::vector<Dependency>> parse_dependencies(
    const toml::table& tbl)
{
    std::vector<Dependency> deps;
    for (const auto& [key, val] : tbl) {
        auto dep = parse_dependency(std::string(key), val);
        if (dep.is_err()) return std::move(dep).error();
        deps.push_back(std::move(dep).value());
    }
    return Result<std::vector<Dependency>>::ok(std::move(deps));
}

static Result<SourceGroup> parse_source_group(const toml::table& tbl) {
    SourceGroup sg;

    // Optional target expression
    if (auto t = tbl["target"].value<std::string>()) {
        auto expr = TargetExpr::parse(std::string(*t));
        if (expr.is_err()) return std::move(expr).error();
        sg.target = std::move(expr).value();
    }

    // files array
    if (auto arr = tbl["files"].as_array()) {
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                sg.files.push_back(std::string(*s));
            }
        }
    }

    // include_dirs array
    if (auto arr = tbl["include_dirs"].as_array()) {
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                sg.include_dirs.push_back(std::string(*s));
            }
        }
    }

    // defines array
    if (auto arr = tbl["defines"].as_array()) {
        for (const auto& elem : *arr) {
            if (auto s = elem.value<std::string>()) {
                sg.defines.push_back(std::string(*s));
            }
        }
    }

    return Result<SourceGroup>::ok(std::move(sg));
}

static TargetConfig parse_target_config(const std::string& name,
                                         const toml::table& tbl) {
    TargetConfig tc;
    tc.name = name;
    if (auto v = tbl["tool"].value<std::string>()) tc.tool = std::string(*v);
    if (auto v = tbl["action"].value<std::string>()) tc.action = std::string(*v);

    // Flatten options subtable to key-value strings
    if (auto opts = tbl["options"].as_table()) {
        for (const auto& [k, v] : *opts) {
            std::string key(k);
            if (v.is_string()) {
                tc.options[key] = std::string(*v.value<std::string>());
            } else if (v.is_boolean()) {
                tc.options[key] = *v.value<bool>() ? "true" : "false";
            } else if (v.is_integer()) {
                tc.options[key] = std::to_string(*v.value<int64_t>());
            } else if (v.is_array()) {
                // Serialize string arrays as comma-separated
                std::string joined;
                if (auto arr = v.as_array()) {
                    for (const auto& elem : *arr) {
                        if (auto s = elem.value<std::string>()) {
                            if (!joined.empty()) joined += ",";
                            joined += std::string(*s);
                        }
                    }
                }
                tc.options[key] = joined;
            }
        }
    }

    return tc;
}

// ---------------------------------------------------------------------------
// Manifest::parse
// ---------------------------------------------------------------------------

Result<Manifest> Manifest::parse(const std::string& toml_str) {
    toml::table doc;
    try {
        doc = toml::parse(toml_str);
    } catch (const toml::parse_error& e) {
        return LoomError{LoomError::Parse,
            std::string("TOML parse error: ") + e.what()};
    }

    Manifest m;

    // [package] section
    if (auto pkg = doc["package"].as_table()) {
        if (auto v = (*pkg)["name"].value<std::string>())
            m.package.name = std::string(*v);
        if (auto v = (*pkg)["version"].value<std::string>())
            m.package.version = std::string(*v);
        if (auto v = (*pkg)["top"].value<std::string>())
            m.package.top = std::string(*v);
        if (auto arr = (*pkg)["authors"].as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.value<std::string>()) {
                    m.package.authors.push_back(std::string(*s));
                }
            }
        }
    }

    // [dependencies] section
    if (auto deps = doc["dependencies"].as_table()) {
        auto parsed = parse_dependencies(*deps);
        if (parsed.is_err()) return std::move(parsed).error();
        m.dependencies = std::move(parsed).value();
    }

    // [[sources]] array-of-tables
    if (auto arr = doc["sources"].as_array()) {
        for (const auto& elem : *arr) {
            if (auto tbl = elem.as_table()) {
                auto sg = parse_source_group(*tbl);
                if (sg.is_err()) return std::move(sg).error();
                m.sources.push_back(std::move(sg).value());
            }
        }
    }

    // [targets.<name>] sections
    if (auto targets = doc["targets"].as_table()) {
        for (const auto& [key, val] : *targets) {
            if (auto tbl = val.as_table()) {
                auto tc = parse_target_config(std::string(key), *tbl);
                m.targets[tc.name] = std::move(tc);
            }
        }
    }

    // [lint] section
    if (auto lint = doc["lint"].as_table()) {
        for (const auto& [key, val] : *lint) {
            std::string k(key);
            if (k == "naming") {
                // [lint.naming] subtable
                if (auto naming = val.as_table()) {
                    for (const auto& [nk, nv] : *naming) {
                        if (auto s = nv.value<std::string>()) {
                            m.lint.naming[std::string(nk)] = std::string(*s);
                        }
                    }
                }
            } else {
                if (auto s = val.value<std::string>()) {
                    m.lint.rules[k] = std::string(*s);
                }
            }
        }
    }

    // [build] section
    if (auto build = doc["build"].as_table()) {
        if (auto v = (*build)["pre-lint"].value<bool>()) m.build.pre_lint = *v;
        if (auto v = (*build)["lint-fatal"].value<bool>()) m.build.lint_fatal = *v;
    }

    // [workspace] section
    if (auto ws = doc["workspace"].as_table()) {
        WorkspaceConfig wc;
        if (auto arr = (*ws)["members"].as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.value<std::string>()) {
                    wc.members.push_back(std::string(*s));
                }
            }
        }
        if (auto arr = (*ws)["exclude"].as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.value<std::string>()) {
                    wc.exclude.push_back(std::string(*s));
                }
            }
        }
        if (auto arr = (*ws)["default-members"].as_array()) {
            for (const auto& elem : *arr) {
                if (auto s = elem.value<std::string>()) {
                    wc.default_members.push_back(std::string(*s));
                }
            }
        }
        if (auto wdeps = (*ws)["dependencies"].as_table()) {
            auto parsed = parse_dependencies(*wdeps);
            if (parsed.is_err()) return std::move(parsed).error();
            wc.dependencies = std::move(parsed).value();
        }
        m.workspace = std::move(wc);
    }

    return Result<Manifest>::ok(std::move(m));
}

// ---------------------------------------------------------------------------
// Manifest::load
// ---------------------------------------------------------------------------

Result<Manifest> Manifest::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return LoomError{LoomError::IO,
            "cannot open manifest file: " + path};
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return Manifest::parse(ss.str());
}

bool Manifest::is_workspace() const {
    return workspace.has_value();
}

} // namespace loom
