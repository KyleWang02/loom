#include <loom/cli.hpp>
#include <loom/log.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    using namespace loom;

    CliParser cli("loom", "0.1.0");

    // Global flags
    cli.add_global_flag({"verbose", "v", "Increase output verbosity", false, "", "", true});
    cli.add_global_flag({"help", "h", "Print help information"});
    cli.add_global_flag({"version", "", "Print version information"});
    cli.add_global_flag({"no-local", "", "Suppress Loom.local overrides"});
    cli.add_global_flag({"offline", "", "Use cached repos only, no network"});
    cli.add_global_flag({"target", "t", "Active target expression", true, "EXPR"});

    // Register all commands
    register_new(cli);
    register_init(cli);
    register_info(cli);
    register_env(cli);
    register_config(cli);
    register_lock(cli);
    register_update(cli);
    register_tree(cli);
    register_clean(cli);
    register_build(cli);
    register_plan(cli);
    register_lint(cli);
    register_doc(cli);

    // Set verbosity from global flags
    // (We need a pre-parse for this, but the handler will use global_args)

    auto result = cli.run(argc, argv);
    if (result.is_err()) {
        auto& err = result.error();
        log::error("%s", err.message.c_str());
        if (!err.hint.empty()) {
            std::cerr << "  hint: " << err.hint << "\n";
        }
        return 1;
    }
    return result.value();
}
