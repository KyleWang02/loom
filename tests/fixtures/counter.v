/// A simple counter module.
///
/// @param WIDTH Bit width of the counter.
module counter #(
    parameter WIDTH = 8
)(
    input  wire             clk,
    input  wire             rst,
    input  wire             en,
    output reg [WIDTH-1:0]  count
);

    // loom: ignore[blocking-in-ff]
    always @(posedge clk or posedge rst) begin : counter_logic
        if (rst)
            count <= {WIDTH{1'b0}};
        else if (en)
            count <= count + 1;
    end

endmodule
