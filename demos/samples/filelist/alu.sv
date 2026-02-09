// Role: Leaf module — imports soc_pkg
// Depends on: soc_pkg (for alu_op_t, DATA_WIDTH)
// Depended on by: datapath

`default_nettype none

module alu
    import soc_pkg::*;
(
    input  logic [DATA_WIDTH-1:0] a,
    input  logic [DATA_WIDTH-1:0] b,
    input  alu_op_t               op,
    output logic [DATA_WIDTH-1:0] result,
    output logic                  zero
);
    always_comb begin
        case (op)
            OP_ADD:  result = a + b;
            OP_SUB:  result = a - b;
            OP_AND:  result = a & b;
            OP_OR:   result = a | b;
            default: result = '0;
        endcase
        zero = (result == '0);
    end
endmodule

`default_nettype wire
