// Tests: case-missing-default (warn), casex-usage (warn)
// A case statement without a default branch can cause latches in synthesis.
// casex treats X/Z as don't-care, which can mask real bugs — prefer casez.

module case_problems (
    input  logic [1:0] sel,
    input  logic [7:0] a, b, c,
    output logic [7:0] y,
    output logic [7:0] z
);
    // Missing default — synthesizes a latch for y
    always_comb begin
        case (sel)
            2'b00: y = a;
            2'b01: y = b;
            2'b10: y = c;
            // no 2'b11 and no default!
        endcase
    end

    // casex is dangerous — prefer casez or case inside
    always_comb begin
        casex (sel)
            2'b0x: z = a;
            2'b1x: z = b;
            default: z = c;
        endcase
    end
endmodule
