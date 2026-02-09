// Role: Mid-level module — instantiates alu + register_file
// Depends on: soc_pkg, alu, register_file
// Depended on by: soc_top

`default_nettype none

module datapath
    import soc_pkg::*;
(
    input  logic                  clk,
    input  logic                  rst_n,
    input  alu_op_t               alu_op,
    input  logic                  reg_we,
    input  logic [3:0]            waddr,
    input  logic [3:0]            raddr1,
    input  logic [3:0]            raddr2,
    input  logic [DATA_WIDTH-1:0] imm,
    input  logic                  use_imm,
    output logic [DATA_WIDTH-1:0] result,
    output logic                  zero
);
    logic [DATA_WIDTH-1:0] rd1, rd2, alu_b;

    register_file #(.NUM_REGS(16)) rf (
        .clk    (clk),
        .rst_n  (rst_n),
        .we     (reg_we),
        .waddr  (waddr),
        .wdata  (result),
        .raddr1 (raddr1),
        .raddr2 (raddr2),
        .rdata1 (rd1),
        .rdata2 (rd2)
    );

    assign alu_b = use_imm ? imm : rd2;

    alu alu_inst (
        .a      (rd1),
        .b      (alu_b),
        .op     (alu_op),
        .result (result),
        .zero   (zero)
    );
endmodule

`default_nettype wire
