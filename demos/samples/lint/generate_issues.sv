// Tests: unlabeled-generate (warn), label-mismatch (warn)
// Generate blocks without labels produce tool-dependent hierarchical names,
// making waveform debugging and constraint writing fragile.
// Mismatched begin/end labels are confusing and error-prone.

module gen_problems #(
    parameter int WIDTH = 8
) (
    input  logic [WIDTH-1:0] a,
    output logic [WIDTH-1:0] y
);
    // Unlabeled generate — linter should flag this
    genvar i;
    generate
        for (i = 0; i < WIDTH; i++) begin
            assign y[i] = ~a[i];
        end
    endgenerate

    // Label mismatch — begin label doesn't match end label
    generate
        for (i = 0; i < WIDTH; i++) begin : gen_buf
            assign y[i] = a[i];
        end : gen_wrong_label
    endgenerate
endmodule
