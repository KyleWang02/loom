// Tests: defparam-usage (warn), always-star (warn)
// defparam is deprecated in modern Verilog — use #() parameter override.
// always @* is the old Verilog-2001 sensitivity — prefer always_comb in SV.

module old_style (
    input  wire [7:0] a,
    input  wire [7:0] b,
    output reg  [7:0] sum
);
    // always @* — linter suggests always_comb
    always @* begin
        sum = a + b;
    end
endmodule

module uses_defparam;
    old_style u0 (.a(8'd1), .b(8'd2), .sum());

    // defparam — linter suggests #() override instead
    defparam u0.SOME_PARAM = 42;
endmodule
