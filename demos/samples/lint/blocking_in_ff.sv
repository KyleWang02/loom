// Tests: blocking-in-ff (error)
// A flip-flop (always_ff) must use nonblocking assignments (<=).
// Using blocking (=) here is a classic RTL bug — simulation may differ
// from synthesis because blocking updates happen immediately.

module bad_ff (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            q = 1'b0;   // BUG: should be <=
        else
            q = d;       // BUG: should be <=
    end
endmodule
