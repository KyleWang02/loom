// Tests: unused-signal (warn), undriven-signal (warn)
// Signals that are declared but never read waste area.
// Signals that are declared but never driven will be X in simulation
// and tied to 0 or removed in synthesis.

module signal_problems (
    input  logic clk,
    input  logic [7:0] data_in,
    output logic [7:0] data_out
);
    logic [7:0] temp;        // assigned but never read (unused)
    logic [7:0] floating;    // declared but never driven (undriven)
    logic [7:0] good_sig;    // properly used

    always_ff @(posedge clk) begin
        temp     <= data_in;       // written but never read
        good_sig <= data_in + 1;
    end

    assign data_out = good_sig | floating;  // floating is read but never driven
endmodule
