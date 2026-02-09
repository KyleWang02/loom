// Tests: suppression via "// loom: ignore[rule-id]"
// Any lint diagnostic can be silenced per-line using a suppression comment.
// The suppression applies to the same line or the very next line.
// Use "// loom: ignore[*]" to suppress all rules on a line.

`default_nettype none   // keeps implicit-net rule happy for this file

module suppression_example (
    input  logic clk,
    input  logic rst_n,
    input  logic d,
    output logic q,
    output logic q2
);
    // This blocking-in-ff is intentionally suppressed
    always_ff @(posedge clk) begin
        // loom: ignore[blocking-in-ff]
        q = d;
    end

    // This one is NOT suppressed — should still fire
    always_ff @(posedge clk) begin
        q2 = d;
    end
endmodule
