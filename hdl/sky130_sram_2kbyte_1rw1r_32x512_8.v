
// BRAM model - drop-in replacement for sky130_sram_2kbyte_1rw1r_32x512_8
// Words: 512
// Word size: 32 bits (4 bytes)
// Total: 2KB
// Port 0: RW only, write mask hardwired to 4'b1111

module sky130_sram_2kbyte_1rw1r_32x512_8 (
`ifdef USE_POWER_PINS
    vccd1,
    vssd1,
`endif
    clk0,
    csb0,
    web0,
    wmask0,     // kept for port compatibility, ignored internally
    addr0,
    din0,
    dout0
);

`ifdef USE_POWER_PINS
    inout vccd1;
    inout vssd1;
`endif

    input  wire        clk0;
    input  wire        csb0;
    input  wire        web0;
    input  wire [3:0]  wmask0;  // ignored, always 4'b1111
    input  wire [8:0]  addr0;
    input  wire [31:0] din0;
    output reg  [31:0] dout0;

    (* ram_style = "block" *)
    reg [31:0] mem [0:511];

    wire [3:0] wmask0_unused;
    assign wmask0_unused = wmask0;  // suppress unused input warning

    always @(posedge clk0) begin
        if (!csb0) begin
            if (!web0) begin
                mem[addr0] <= din0;  // full 32-bit write, no masking
            end
            dout0 <= mem[addr0];
        end
    end

endmodule