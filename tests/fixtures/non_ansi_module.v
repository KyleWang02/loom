module legacy_adder(a, b, cin, sum, cout);
    parameter WIDTH = 8;

    input  [WIDTH-1:0] a;
    input  [WIDTH-1:0] b;
    input              cin;
    output [WIDTH-1:0] sum;
    output             cout;

    wire [WIDTH:0] result;

    assign result = a + b + cin;
    assign sum    = result[WIDTH-1:0];
    assign cout   = result[WIDTH];

endmodule
