/// Outer module with nested submodule.
module outer #(
    parameter WIDTH = 8
)(
    input  logic        clk,
    input  logic        rst_n,
    output logic [WIDTH-1:0] data
);

    module inner(
        input  wire in_a,
        output wire out_b
    );
        assign out_b = in_a;
    endmodule

    inner u_inner(.in_a(clk), .out_b());

endmodule
