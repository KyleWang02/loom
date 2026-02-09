// Tests: a clean file that should produce ZERO lint warnings.
// Use this as a baseline to confirm the linter doesn't false-positive.

`default_nettype none

module clean_counter #(
    parameter int WIDTH = 8
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic             en,
    output logic [WIDTH-1:0] count
);
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            count <= '0;
        else if (en)
            count <= count + 1;
    end
endmodule

`default_nettype wire
