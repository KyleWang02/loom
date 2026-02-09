// Role: Shared package (leaf of dependency graph)
// The filelist generator should place this FIRST — packages must be
// compiled before any module that imports them.

`default_nettype none

package soc_pkg;
    typedef enum logic [1:0] {
        OP_ADD = 2'b00,
        OP_SUB = 2'b01,
        OP_AND = 2'b10,
        OP_OR  = 2'b11
    } alu_op_t;

    typedef struct packed {
        logic        valid;
        logic [31:0] data;
        logic [3:0]  tag;
    } bus_pkt_t;

    parameter int DATA_WIDTH = 32;
    parameter int ADDR_WIDTH = 16;
endpackage

`default_nettype wire
