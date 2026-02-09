// Tests: implicit-net (warn)
// Without `default_nettype none, undeclared wires are silently created
// as implicit nets. This hides typos — a misspelled signal name
// becomes a dangling 1-bit wire instead of a compile error.
//
// This file deliberately omits the directive so the linter flags it.

module implicit_danger (
    input  wire clk,
    input  wire data_in,
    output wire data_out
);
    assign data_out = data_in;
endmodule
