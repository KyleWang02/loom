// Tests: nonblocking-in-comb (error)
// Combinational logic (always_comb) must use blocking assignments (=).
// Using nonblocking (<=) in combinational blocks causes simulation
// mismatches — the value isn't available until the next delta cycle.

module bad_comb (
    input  logic a,
    input  logic b,
    input  logic sel,
    output logic y
);
    always_comb begin
        if (sel)
            y <= a;   // BUG: should be =
        else
            y <= b;   // BUG: should be =
    end
endmodule
