// Role: Leaf module — imports soc_pkg
// Depends on: soc_pkg (for DATA_WIDTH)
// Depended on by: datapath

`default_nettype none

module register_file
    import soc_pkg::*;
#(
    parameter int NUM_REGS = 16
) (
    input  logic                          clk,
    input  logic                          rst_n,
    input  logic                          we,
    input  logic [$clog2(NUM_REGS)-1:0]   waddr,
    input  logic [DATA_WIDTH-1:0]         wdata,
    input  logic [$clog2(NUM_REGS)-1:0]   raddr1,
    input  logic [$clog2(NUM_REGS)-1:0]   raddr2,
    output logic [DATA_WIDTH-1:0]         rdata1,
    output logic [DATA_WIDTH-1:0]         rdata2
);
    logic [DATA_WIDTH-1:0] regs [NUM_REGS];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (int i = 0; i < NUM_REGS; i++)
                regs[i] <= '0;
        end else if (we) begin
            regs[waddr] <= wdata;
        end
    end

    assign rdata1 = regs[raddr1];
    assign rdata2 = regs[raddr2];
endmodule

`default_nettype wire
