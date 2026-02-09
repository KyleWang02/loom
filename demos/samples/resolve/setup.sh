#!/usr/bin/env bash
# Creates local git repos that act as loom dependencies.
# Run this ONCE before using: loom-demo resolve demos/samples/resolve/myproject
#
# Layout after running:
#   resolve/
#     deps/
#       uart_ip/       <- git repo, v1.0.0 tag, has Loom.toml
#       spi_ip/        <- git repo, v0.2.0 tag, has Loom.toml
#       common_lib/    <- git repo, v0.1.0 tag, has Loom.toml (no deps)
#     myproject/
#       Loom.toml      <- depends on uart_ip + spi_ip

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_DIR="$SCRIPT_DIR/deps"
PROJECT_DIR="$SCRIPT_DIR/myproject"

rm -rf "$DEPS_DIR"
mkdir -p "$DEPS_DIR"

# ─── common_lib (leaf dependency, no deps of its own) ─────────────
echo "Creating common_lib..."
mkdir -p "$DEPS_DIR/common_lib"
cd "$DEPS_DIR/common_lib"
git init -q
cat > Loom.toml << 'TOML'
[package]
name = "common_lib"
version = "0.1.0"
description = "Shared utility cells"
TOML

mkdir -p src
cat > src/util_cells.sv << 'SV'
`default_nettype none
module sync_ff #(parameter int STAGES = 2) (
    input  logic clk,
    input  logic d,
    output logic q
);
    logic [STAGES-1:0] chain;
    always_ff @(posedge clk) chain <= {chain[STAGES-2:0], d};
    assign q = chain[STAGES-1];
endmodule
`default_nettype wire
SV

git add -A && git commit -q -m "initial common_lib" && git tag v0.1.0
echo "  -> $DEPS_DIR/common_lib (v0.1.0)"

# ─── uart_ip (depends on common_lib) ──────────────────────────────
echo "Creating uart_ip..."
mkdir -p "$DEPS_DIR/uart_ip"
cd "$DEPS_DIR/uart_ip"
git init -q
cat > Loom.toml << TOML
[package]
name = "uart_ip"
version = "1.0.0"
description = "UART transmitter/receiver"

[dependencies]
common_lib = { path = "$DEPS_DIR/common_lib", version = ">=0.1.0" }
TOML

mkdir -p src
cat > src/uart_tx.sv << 'SV'
`default_nettype none
module uart_tx #(parameter int CLK_DIV = 868) (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data,
    input  logic       valid,
    output logic       tx,
    output logic       ready
);
    assign tx = 1'b1;
    assign ready = 1'b1;
endmodule
`default_nettype wire
SV

git add -A && git commit -q -m "initial uart_ip" && git tag v1.0.0
echo "  -> $DEPS_DIR/uart_ip (v1.0.0)"

# ─── spi_ip (depends on common_lib) ───────────────────────────────
echo "Creating spi_ip..."
mkdir -p "$DEPS_DIR/spi_ip"
cd "$DEPS_DIR/spi_ip"
git init -q
cat > Loom.toml << TOML
[package]
name = "spi_ip"
version = "0.2.0"
description = "SPI master controller"

[dependencies]
common_lib = { path = "$DEPS_DIR/common_lib", version = ">=0.1.0" }
TOML

mkdir -p src
cat > src/spi_master.sv << 'SV'
`default_nettype none
module spi_master (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] mosi_data,
    input  logic       start,
    output logic       sclk,
    output logic       mosi,
    input  logic       miso,
    output logic       cs_n,
    output logic       done
);
    assign sclk = 1'b0;
    assign mosi = 1'b0;
    assign cs_n = 1'b1;
    assign done = 1'b0;
endmodule
`default_nettype wire
SV

git add -A && git commit -q -m "initial spi_ip" && git tag v0.2.0
echo "  -> $DEPS_DIR/spi_ip (v0.2.0)"

# ─── myproject (the root project) ─────────────────────────────────
echo "Creating myproject..."
mkdir -p "$PROJECT_DIR"
cat > "$PROJECT_DIR/Loom.toml" << TOML
[package]
name = "myproject"
version = "0.1.0"
description = "Demo SoC project"

[dependencies]
uart_ip = { path = "$DEPS_DIR/uart_ip", version = ">=1.0.0" }
spi_ip  = { path = "$DEPS_DIR/spi_ip",  version = ">=0.2.0" }
TOML

echo ""
echo "Done! Dependency graph:"
echo "  myproject"
echo "    ├── uart_ip v1.0.0"
echo "    │   └── common_lib v0.1.0"
echo "    └── spi_ip v0.2.0"
echo "        └── common_lib v0.1.0  (shared, resolved once)"
echo ""
echo "Run:  ./build/loom-demo resolve demos/samples/resolve/myproject"
