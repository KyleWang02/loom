// Tests: mixed-blocking (error)
// Mixing blocking (=) and nonblocking (<=) in the same always block
// is almost always a mistake. Pick one style per block.

module mixed_assigns (
    input  logic clk,
    input  logic a, b,
    output logic x, y
);
    always_ff @(posedge clk) begin
        x <= a;    // nonblocking — correct for FF
        y = b;     // BUG: blocking mixed into same block
    end
endmodule
