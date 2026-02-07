/// Module exercising lint-relevant constructs.
module lint_test(
    input  logic       clk,
    input  logic       rst_n,
    input  logic       en,
    input  logic [1:0] sel,
    input  logic       d,
    output logic       q,
    output logic       y
);

    logic internal_sig;

    // always_comb with blocking assignments (correct)
    always_comb begin : comb_block
        y = 1'b0;
        case (sel)
            2'b00: y = d;
            2'b01: y = ~d;
            default: y = 1'b0;
        endcase
    end

    // always_ff with non-blocking assignments (correct)
    always_ff @(posedge clk or negedge rst_n) begin : ff_block
        if (!rst_n)
            q <= 1'b0;
        else if (en)
            q <= d;
    end

    // always_latch
    always_latch begin : latch_block
        if (en) q <= d;
    end

    // unique case
    always_comb begin
        unique case (sel)
            2'b00: internal_sig = 1'b0;
            2'b01: internal_sig = 1'b1;
            2'b10: internal_sig = d;
            2'b11: internal_sig = ~d;
        endcase
    end

    // priority case
    always_comb begin
        priority case (sel)
            2'b00: internal_sig = 1'b0;
            default: internal_sig = 1'b1;
        endcase
    end

    // Generate with label
    generate
        for (genvar i = 0; i < 2; i++) begin : gen_labeled
        end
    endgenerate

    // Generate without label
    generate
        for (genvar i = 0; i < 2; i++) begin
        end
    endgenerate

    // defparam usage (deprecated)
    defparam u_sub.WIDTH = 16;

endmodule
