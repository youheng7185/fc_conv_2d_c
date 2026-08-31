// // module fc_filter_rom (
// // `ifdef USE_POWER_PINS
// //     vccd1,
// //     vssd1,
// // `endif
// //     clk0,
// //     csb0,
// //     addr0,    // 12-bit: covers 4000 words (byte addr 0..15999)
// //     dout0
// // );

// // `ifdef USE_POWER_PINS
// //     inout vccd1;
// //     inout vssd1;
// // `endif

// //     input  wire        clk0;
// //     input  wire        csb0;
// //     input  wire [11:0] addr0;   // byte address 0..15999
// //     output reg  [7:0]  dout0;   // single byte output

// //     // 8 BRAM instances, each 512 words × 32 bits = 2KB
// //     (* rom_style = "block" *) reg [31:0] mem0 [0:511];   // bytes    0.. 2047
// //     (* rom_style = "block" *) reg [31:0] mem1 [0:511];   // bytes 2048.. 4095
// //     (* rom_style = "block" *) reg [31:0] mem2 [0:511];   // bytes 4096.. 6143
// //     (* rom_style = "block" *) reg [31:0] mem3 [0:511];   // bytes 6144.. 8191
// //     (* rom_style = "block" *) reg [31:0] mem4 [0:511];   // bytes 8192..10239
// //     (* rom_style = "block" *) reg [31:0] mem5 [0:511];   // bytes 10240..12287
// //     (* rom_style = "block" *) reg [31:0] mem6 [0:511];   // bytes 12288..14335
// //     (* rom_style = "block" *) reg [31:0] mem7 [0:511];   // bytes 14336..15999 (partial)

// //     initial begin
// //         $readmemh("fc_filter_0.mem", mem0);
// //         $readmemh("fc_filter_1.mem", mem1);
// //         $readmemh("fc_filter_2.mem", mem2);
// //         $readmemh("fc_filter_3.mem", mem3);
// //         $readmemh("fc_filter_4.mem", mem4);
// //         $readmemh("fc_filter_5.mem", mem5);
// //         $readmemh("fc_filter_6.mem", mem6);
// //         $readmemh("fc_filter_7.mem", mem7);
// //     end

// //     // Pipeline registers to match BRAM read latency
// //     reg [2:0]  bank_sel_r;
// //     reg [1:0]  byte_lane_r;

// //     wire [2:0]  bank_sel  = addr0[11:9];   // which 2KB bank (0..7)
// //     wire [8:0]  word_addr = addr0[8:2] ;   // word within bank (0..511)
// //     wire [1:0]  byte_lane = addr0[1:0];    // byte within word (0..3)

// //     // BRAM read registers
// //     reg [31:0] bank_dout [0:7];

// //     always @(posedge clk0) begin
// //         if (!csb0) begin
// //             bank_dout[0] <= mem0[word_addr];
// //             bank_dout[1] <= mem1[word_addr];
// //             bank_dout[2] <= mem2[word_addr];
// //             bank_dout[3] <= mem3[word_addr];
// //             bank_dout[4] <= mem4[word_addr];
// //             bank_dout[5] <= mem5[word_addr];
// //             bank_dout[6] <= mem6[word_addr];
// //             bank_dout[7] <= mem7[word_addr];

// //             // Register selectors to match BRAM output latency
// //             bank_sel_r  <= bank_sel;
// //             byte_lane_r <= byte_lane;
// //         end
// //     end

// //     // Byte extraction after registered read
// //     always @(posedge clk0) begin
// //         dout0 <= bank_dout[bank_sel_r] >> (byte_lane_r * 8);
// //     end

// // endmodule

// module fc_filter_rom (
// `ifdef USE_POWER_PINS
//     vccd1,
//     vssd1,
// `endif
//     clk0,
//     csb0,
//     addr0,
//     dout0
// );

// `ifdef USE_POWER_PINS
//     inout vccd1;
//     inout vssd1;
// `endif

//     input  wire        clk0;
//     input  wire        csb0;
//     input  wire [13:0] addr0;   // 14-bit: byte address 0..15999
//     output reg  [7:0]  dout0;

//     (* rom_style = "block" *) reg [31:0] mem0 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem1 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem2 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem3 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem4 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem5 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem6 [0:511];
//     (* rom_style = "block" *) reg [31:0] mem7 [0:511];

//     initial begin
//         $readmemh("fc_filter_0.mem", mem0);
//         $readmemh("fc_filter_1.mem", mem1);
//         $readmemh("fc_filter_2.mem", mem2);
//         $readmemh("fc_filter_3.mem", mem3);
//         $readmemh("fc_filter_4.mem", mem4);
//         $readmemh("fc_filter_5.mem", mem5);
//         $readmemh("fc_filter_6.mem", mem6);
//         $readmemh("fc_filter_7.mem", mem7);
//     end

//     wire [2:0]  bank_sel  = addr0[13:11];  // 14-bit: bits[13:11] = bank 0..7
//     wire [8:0]  word_addr = addr0[10:2];   // bits[10:2]  = word within bank
//     wire [1:0]  byte_lane = addr0[1:0];    // bits[1:0]   = byte within word

//     // Stage 1: BRAM read + register selectors — only when csb active
//     reg [31:0] bank_dout [0:7];
//     reg [2:0]  bank_sel_r;
//     reg [1:0]  byte_lane_r;
//     reg        valid_r;           // tracks whether stage1 had a valid read

//     always @(posedge clk0) begin
//         valid_r     <= !csb0;     // stage1 valid
//         bank_sel_r  <= bank_sel;
//         byte_lane_r <= byte_lane;

//         // BRAM reads always registered — csb just masks the result via valid
//         bank_dout[0] <= mem0[word_addr];
//         bank_dout[1] <= mem1[word_addr];
//         bank_dout[2] <= mem2[word_addr];
//         bank_dout[3] <= mem3[word_addr];
//         bank_dout[4] <= mem4[word_addr];
//         bank_dout[5] <= mem5[word_addr];
//         bank_dout[6] <= mem6[word_addr];
//         bank_dout[7] <= mem7[word_addr];
//     end

//     // Stage 2: bank mux + byte extraction — only when stage1 was valid
//     reg valid_r2;

//     always @(posedge clk0) begin
//         valid_r2 <= valid_r;
//         if (valid_r)
//             dout0 <= bank_dout[bank_sel_r] >> (byte_lane_r * 8);
//         // When valid_r=0, dout0 holds its last value — fc_top
//         // must not use dout0 unless it tracked the latency correctly
//     end

// endmodule

module fc_filter_rom (
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

    input  wire        clk0;
    input  wire        csb0;       // unused internally — kept for compatibility
    input  wire [13:0] addr0;
    output reg  [7:0]  dout0;

    (* rom_style = "block" *) reg [31:0] mem0 [0:511];
    (* rom_style = "block" *) reg [31:0] mem1 [0:511];
    (* rom_style = "block" *) reg [31:0] mem2 [0:511];
    (* rom_style = "block" *) reg [31:0] mem3 [0:511];
    (* rom_style = "block" *) reg [31:0] mem4 [0:511];
    (* rom_style = "block" *) reg [31:0] mem5 [0:511];
    (* rom_style = "block" *) reg [31:0] mem6 [0:511];
    (* rom_style = "block" *) reg [31:0] mem7 [0:511];

    initial begin
        $readmemh("fc_filter_0.mem", mem0);
        $readmemh("fc_filter_1.mem", mem1);
        $readmemh("fc_filter_2.mem", mem2);
        $readmemh("fc_filter_3.mem", mem3);
        $readmemh("fc_filter_4.mem", mem4);
        $readmemh("fc_filter_5.mem", mem5);
        $readmemh("fc_filter_6.mem", mem6);
        $readmemh("fc_filter_7.mem", mem7);
    end

    wire [2:0] bank_sel  = addr0[13:11];
    wire [8:0] word_addr = addr0[10:2];
    wire [1:0] byte_lane = addr0[1:0];

    // Stage 1: unconditional BRAM read every clock
    reg [31:0] bank_dout [0:7];
    reg [2:0]  bank_sel_r;
    reg [1:0]  byte_lane_r;

    always @(posedge clk0) begin
        bank_dout[0] <= mem0[word_addr];
        bank_dout[1] <= mem1[word_addr];
        bank_dout[2] <= mem2[word_addr];
        bank_dout[3] <= mem3[word_addr];
        bank_dout[4] <= mem4[word_addr];
        bank_dout[5] <= mem5[word_addr];
        bank_dout[6] <= mem6[word_addr];
        bank_dout[7] <= mem7[word_addr];
        bank_sel_r   <= bank_sel;
        byte_lane_r  <= byte_lane;
    end

    // Stage 2: unconditional mux + byte extract
    always @(posedge clk0) begin
        dout0 <= bank_dout[bank_sel_r] >> (byte_lane_r * 8);
    end

endmodule