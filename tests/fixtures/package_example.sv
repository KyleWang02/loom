/// Common types package.
package common_types;

    typedef enum logic [1:0] {
        IDLE    = 2'b00,
        ACTIVE  = 2'b01,
        DONE    = 2'b10
    } state_t;

    typedef struct packed {
        logic [7:0] addr;
        logic [31:0] data;
        logic        valid;
    } bus_req_t;

endpackage

/// A simple AXI interface.
interface axi_if #(
    parameter ADDR_W = 32,
    parameter DATA_W = 64
);

    logic [ADDR_W-1:0] awaddr;
    logic               awvalid;
    logic               awready;

    modport master(output awaddr, awvalid, input awready);
    modport slave(input awaddr, awvalid, output awready);

endinterface

module top_module
    import common_types::*;
(
    input  logic        clk,
    input  logic        rst_n,
    output state_t      state,
    axi_if.master       axi
);

    always_comb begin
        state = IDLE;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            state <= IDLE;
        else
            state <= ACTIVE;
    end

endmodule
