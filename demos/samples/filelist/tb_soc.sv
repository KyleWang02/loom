// Role: Testbench — instantiates soc_top
// The filelist generator should detect this as a testbench because:
//   1. Name contains "tb"
//   2. No ports (top-level test harness)
//   3. Located alongside other files (could also be in a tb/ directory)

`default_nettype none

module tb_soc;
    logic clk, rst_n;

    soc_top dut (
        .clk   (clk),
        .rst_n (rst_n)
    );

    // Clock generation
    initial clk = 0;
    always #5 clk = ~clk;

    // Test stimulus
    initial begin
        rst_n = 0;
        #20;
        rst_n = 1;
        #100;
        $display("TEST PASSED");
        $finish;
    end
endmodule

`default_nettype wire
