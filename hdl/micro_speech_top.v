// micro_speech_top.v
// Top-level wrapper: instantiates conv_top and conv_output_bram
// Exposes conv_output_bram read port externally

module micro_speech_top (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        start_i,
    output wire        done_o,

    // Testbench interface — passed through to conv_top
    input  wire        tb_csb_i,
    input  wire        tb_web_i,
    input  wire [8:0]  tb_addr_i,
    input  wire [31:0] tb_din_i,

    // conv_output_bram read port — exposed externally
    input  wire        out_bram_csb_i,
    input  wire [9:0]  out_bram_addr_i,
    output wire [31:0] out_bram_dout_o
);

    // -------------------------------------------------------
    // Internal wires: conv_top → conv_output_bram (write port)
    // -------------------------------------------------------
    wire        conv_out_csb;
    wire        conv_out_web;
    wire [9:0]  conv_out_addr;
    wire [31:0] conv_out_din;

    // -------------------------------------------------------
    // conv_top instance
    // -------------------------------------------------------
    conv_top u_conv_top (
        .clk_i          (clk_i),
        .rst_ni         (rst_ni),
        .start_i        (start_i),
        .done_o         (done_o),

        .tb_csb_i       (tb_csb_i),
        .tb_web_i       (tb_web_i),
        .tb_addr_i      (tb_addr_i),
        .tb_din_i       (tb_din_i),

        .conv_out_csb_o (conv_out_csb),
        .conv_out_web_o (conv_out_web),
        .conv_out_addr_o(conv_out_addr),
        .conv_out_din_o (conv_out_din)
    );

    // -------------------------------------------------------
    // conv_output_bram instance
    // Single-port: write driven by conv_top, read driven externally
    // We mux csb and addr: conv_top owns write, external owns read
    // web=1 (read) when external is reading
    // -------------------------------------------------------
    wire        bram_csb;
    wire        bram_web;
    wire [9:0]  bram_addr;
    wire [31:0] bram_din;

    // conv_top write takes priority; external read when conv not writing
    assign bram_csb  = conv_out_csb & out_bram_csb_i; // active when either asserts
    assign bram_web  = conv_out_web;                   // only conv_top writes
    assign bram_addr = conv_out_csb ? out_bram_addr_i  // conv idle → ext reads
                                    : conv_out_addr;   // conv active → conv writes
    assign bram_din  = conv_out_din;

    conv_output_bram u_conv_output_bram (
        .clk0  (clk_i),
        .csb0  (bram_csb),
        .web0  (bram_web),
        .addr0 (bram_addr),
        .din0  (bram_din),
        .dout0 (out_bram_dout_o)
    );

endmodule