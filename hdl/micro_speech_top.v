// micro_speech_top.v
// Top-level: conv_top → conv_output_bram → fc_top
// FC output is 4 int8 registers wired to top-level ports

module micro_speech_top (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        start_i,
    output wire        done_o,

    input  wire        tb_csb_i,
    input  wire        tb_web_i,
    input  wire [8:0]  tb_addr_i,
    input  wire [31:0] tb_din_i,

    output wire signed [7:0] fc_out_0,
    output wire signed [7:0] fc_out_1,
    output wire signed [7:0] fc_out_2,
    output wire signed [7:0] fc_out_3
);

    wire        conv_out_csb;
    wire        conv_out_web;
    wire [9:0]  conv_out_addr;
    wire [31:0] conv_out_din;

    wire conv_done;
    wire fc_start;
    wire fc_done;

    assign fc_start = conv_done;
    assign done_o   = fc_done;

    conv_top u_conv_top (
        .clk_i           (clk_i),
        .rst_ni          (rst_ni),
        .start_i         (start_i),
        .done_o          (conv_done),

        .tb_csb_i        (tb_csb_i),
        .tb_web_i        (tb_web_i),
        .tb_addr_i       (tb_addr_i),
        .tb_din_i        (tb_din_i),

        .conv_out_csb_o  (conv_out_csb),
        .conv_out_web_o  (conv_out_web),
        .conv_out_addr_o (conv_out_addr),
        .conv_out_din_o  (conv_out_din)
    );

    wire        fc_bram_csb;
    wire [9:0]  fc_bram_addr;
    wire [31:0] fc_bram_dout;

    // conv write takes priority over fc read
    wire       bram_csb  = conv_out_csb & fc_bram_csb;
    wire       bram_web  = conv_out_web;
    wire [9:0] bram_addr = !conv_out_csb ? conv_out_addr : fc_bram_addr;
    wire [31:0] bram_din = conv_out_din;

    conv_output_bram u_conv_output_bram (
        .clk0  (clk_i),
        .csb0  (bram_csb),
        .web0  (bram_web),
        .addr0 (bram_addr),
        .din0  (bram_din),
        .dout0 (fc_bram_dout)
    );

    fc_top u_fc_top (
        .clk_i       (clk_i),
        .rst_ni      (rst_ni),
        .start_i     (fc_start),
        .done_o      (fc_done),

        .bram_csb_o  (fc_bram_csb),
        .bram_addr_o (fc_bram_addr),
        .bram_dout_i (fc_bram_dout),

        .fc_out_0    (fc_out_0),
        .fc_out_1    (fc_out_1),
        .fc_out_2    (fc_out_2),
        .fc_out_3    (fc_out_3)
    );

endmodule