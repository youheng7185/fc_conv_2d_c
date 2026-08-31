module conv_filter_rom (
`ifdef USE_POWER_PINS
    vccd1,
    vssd1,
`endif
    clk0,
    csb0,
    addr0,
    dout0
);

`ifdef USE_POWER_PINS
    inout vccd1;
    inout vssd1;
`endif

    input  wire       clk0;
    input  wire       csb0;
    input  wire [8:0] addr0;
    output reg [31:0] dout0;

    (* rom_style = "block" *)
    reg [31:0] mem [0:511];

    initial begin
        $readmemh("conv_filter.mem", mem);
    end

    always @(posedge clk0) begin
        if (!csb0) begin
            dout0 <= mem[addr0];
        end
    end

endmodule