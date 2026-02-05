module counter #(
    parameter WIDTH = 8
)(
    input  wire             clk,
    input  wire             rst,
    input  wire             en,
    output reg [WIDTH-1:0]  count
);

    always @(posedge clk or posedge rst) begin
        if (rst)
            count <= 0;
        else if (en)
            count <= count + 1;
    end

endmodule
