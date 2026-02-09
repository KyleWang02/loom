// Role: Top-level module — instantiates datapath + memory_ctrl (black box)
// Depends on: soc_pkg, datapath, memory_ctrl (not provided = black box)
// Depended on by: nothing (this IS the top)
// The filelist generator should detect this as the top module (in-degree 0)
// and flag memory_ctrl as a black box (instantiated but never defined).

`default_nettype none

module soc_top
    import soc_pkg::*;
(
    input  logic clk,
    input  logic rst_n
);
    alu_op_t               alu_op;
    logic                  reg_we, use_imm, zero;
    logic [3:0]            waddr, raddr1, raddr2;
    logic [DATA_WIDTH-1:0] imm, result;

    datapath dp (
        .clk     (clk),
        .rst_n   (rst_n),
        .alu_op  (alu_op),
        .reg_we  (reg_we),
        .waddr   (waddr),
        .raddr1  (raddr1),
        .raddr2  (raddr2),
        .imm     (imm),
        .use_imm (use_imm),
        .result  (result),
        .zero    (zero)
    );

    // memory_ctrl is intentionally NOT defined in any file —
    // the filelist generator should report it as a black box.
    memory_ctrl mem (
        .clk     (clk),
        .rst_n   (rst_n),
        .addr    (result[ADDR_WIDTH-1:0]),
        .wdata   (result),
        .rdata   ()
    );

    // Stub control signals for demo purposes
    assign alu_op  = OP_ADD;
    assign reg_we  = 1'b0;
    assign use_imm = 1'b0;
    assign waddr   = 4'd0;
    assign raddr1  = 4'd0;
    assign raddr2  = 4'd1;
    assign imm     = '0;
endmodule

`default_nettype wire
