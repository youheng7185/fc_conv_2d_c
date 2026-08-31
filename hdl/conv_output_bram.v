// conv_output_bram.v
// 4KB BRAM: 1024 words x 32 bits
// 4000 bytes used for conv output (25 x 20 x 8)
// Addresses 4000..4095 unused

module conv_output_bram (
    input  wire        clk0,
    input  wire        csb0,
    input  wire        web0,
    input  wire [9:0]  addr0,
    input  wire [31:0] din0,
    output reg  [31:0] dout0
);
    (* ram_style = "block" *)
    reg [31:0] mem [0:1023];

    always @(posedge clk0) begin
        if (!csb0) begin
            if (!web0)
                mem[addr0] <= din0;
            dout0 <= mem[addr0];
        end
    end

endmodule