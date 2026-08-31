// conv_top
// Convolution stage
// Internally instantiates: input_bram, conv_filter_rom
// Externally connects to:  conv_output_bram (ports exposed)

module conv_top (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        start_i,
    output reg         done_o,

    input  wire        tb_csb_i,
    input  wire        tb_web_i,
    input  wire [8:0]  tb_addr_i,
    input  wire [31:0] tb_din_i,

    output reg         conv_out_csb_o,
    output reg         conv_out_web_o,
    output reg  [9:0]  conv_out_addr_o,
    output reg  [31:0] conv_out_din_o
);

    // ---------------------------------------------------------------
    // Parameters
    // ---------------------------------------------------------------
    localparam INPUT_W       = 6'd40;
    localparam INPUT_H       = 6'd49;
    localparam FILTER_W      = 4'd8;
    localparam FILTER_H      = 4'd10;
    localparam OUTPUT_W      = 5'd20;
    localparam OUTPUT_H      = 5'd25;
    localparam OUTPUT_C      = 4'd8;
    localparam STRIDE_W      = 2'd2;
    localparam STRIDE_H      = 2'd2;
    localparam PAD_W         = 3'd3;
    localparam PAD_H         = 3'd4;
    localparam signed [31:0] INPUT_OFFSET  =  32'sd128;
    localparam signed [31:0] OUTPUT_OFFSET = -32'sd128;
    localparam signed [31:0] ACT_MIN       = -32'sd128;
    localparam signed [31:0] ACT_MAX       =  32'sd127;

    // ---------------------------------------------------------------
    // Per-channel LUTs
    // ---------------------------------------------------------------
    reg signed [31:0] MULTIPLIER [0:7];
    reg        [4:0]  SHIFT      [0:7];
    reg signed [31:0] BIAS       [0:7];

    initial begin
        MULTIPLIER[0] = 32'sd1653229999; SHIFT[0] = 5'd10;
        MULTIPLIER[1] = 32'sd1516545207; SHIFT[1] = 5'd12;
        MULTIPLIER[2] = 32'sd2000799311; SHIFT[2] = 5'd10;
        MULTIPLIER[3] = 32'sd1159928266; SHIFT[3] = 5'd10;
        MULTIPLIER[4] = 32'sd1498403863; SHIFT[4] = 5'd10;
        MULTIPLIER[5] = 32'sd1285645282; SHIFT[5] = 5'd10;
        MULTIPLIER[6] = 32'sd2146175029; SHIFT[6] = 5'd10;
        MULTIPLIER[7] = 32'sd1756589032; SHIFT[7] = 5'd10;

        BIAS[0] = -32'sd374;
        BIAS[1] =  32'sd169;
        BIAS[2] = -32'sd48;
        BIAS[3] =  32'sd208;
        BIAS[4] =  32'sd82;
        BIAS[5] =  32'sd6;
        BIAS[6] = -32'sd1201;
        BIAS[7] = -32'sd694;
    end

    // ---------------------------------------------------------------
    // Input BRAM signals
    // ---------------------------------------------------------------
    wire        in_bram_csb;
    wire        in_bram_web;
    wire [8:0]  in_bram_addr;
    wire [31:0] in_bram_din;
    wire [31:0] in_bram_dout;

    reg         eng_in_csb;
    reg [8:0]   eng_in_addr;

    assign in_bram_csb  = (state == S_IDLE) ? tb_csb_i    : eng_in_csb;
    assign in_bram_web  = (state == S_IDLE) ? tb_web_i    : 1'b1;
    assign in_bram_addr = (state == S_IDLE) ? tb_addr_i   : eng_in_addr;
    assign in_bram_din  = (state == S_IDLE) ? tb_din_i    : 32'd0;

    sky130_sram_2kbyte_1rw1r_32x512_8 input_bram (
        .clk0   (clk_i),
        .csb0   (in_bram_csb),
        .web0   (in_bram_web),
        .wmask0 (4'b1111),
        .addr0  (in_bram_addr),
        .din0   (in_bram_din),
        .dout0  (in_bram_dout)
    );

    // ---------------------------------------------------------------
    // Filter ROM signals
    // ---------------------------------------------------------------
    reg        filt_csb;
    reg [8:0]  filt_addr;
    wire [31:0] filt_dout;

    conv_filter_rom filter_rom (
        .clk0  (clk_i),
        .csb0  (filt_csb),
        .addr0 (filt_addr),
        .dout0 (filt_dout)
    );

    // ---------------------------------------------------------------
    // FSM states
    // ---------------------------------------------------------------
    localparam S_IDLE        = 4'd0;
    localparam S_LOAD_IN     = 4'd1;
    localparam S_WAIT_BRAM   = 4'd2;
    localparam S_MAC         = 4'd3;
    localparam S_BIAS        = 4'd4;
    localparam S_QUANT_MUL   = 4'd5;
    localparam S_QUANT_SHIFT = 4'd6;
    localparam S_WRITE       = 4'd7;
    localparam S_DONE        = 4'd8;

    reg [3:0] state;

    // ---------------------------------------------------------------
    // Loop counters
    // ---------------------------------------------------------------
    reg [4:0] out_y;
    reg [4:0] out_x;
    reg [2:0] out_c;
    reg [3:0] fy;
    reg [2:0] fx;

    // ---------------------------------------------------------------
    // Coordinate logic
    // ---------------------------------------------------------------
    wire signed [7:0] in_x_sig = $signed({2'b0, out_x}) * STRIDE_W
                                  - $signed({1'b0, PAD_W})
                                  + $signed({1'b0, fx});

    wire signed [7:0] in_y_sig = $signed({2'b0, out_y}) * STRIDE_H
                                  - $signed({1'b0, PAD_H})
                                  + $signed({1'b0, fy});

    wire in_bounds = (in_x_sig >= 0) && (in_x_sig < $signed(8'd40)) &&
                     (in_y_sig >= 0) && (in_y_sig < $signed(8'd49));

    wire [5:0] in_x = in_x_sig[5:0];
    wire [5:0] in_y = in_y_sig[5:0];

    wire [10:0] in_byte_addr  = (in_y * 6'd40) + {5'd0, in_x};
    wire [8:0]  in_word_addr  = in_byte_addr[10:2];
    wire [1:0]  in_byte_lane  = in_byte_addr[1:0];

    wire [9:0]  filt_byte_addr = ({2'b0, fy, 3'b0} + {4'b0, fx}) * 4'd8
                                  + {7'b0, out_c};
    wire [8:0]  filt_word_addr = filt_byte_addr[9:2];
    wire [1:0]  filt_byte_lane = filt_byte_addr[1:0];

    wire [11:0] out_byte_addr  = ({2'b0, out_y} * 5'd20 + {2'b0, out_x}) * 4'd8
                                  + {9'b0, out_c};
    wire [9:0]  out_word_addr  = out_byte_addr[11:2];
    wire [1:0]  out_byte_lane  = out_byte_addr[1:0];

    // ---------------------------------------------------------------
    // Registered pipeline signals
    // ---------------------------------------------------------------
    reg [1:0]  in_byte_lane_r;
    reg [1:0]  filt_byte_lane_r;
    reg        in_bounds_r;

    // wire signed [8:0] input_val =
    //     $signed({1'b0, in_bram_dout[in_byte_lane_r*8 +: 8]}) + INPUT_OFFSET;

    wire signed [8:0] input_val =
        $signed(in_bram_dout[in_byte_lane_r*8 +: 8]) + INPUT_OFFSET;

    wire signed [7:0] filter_val =
        $signed(filt_dout[filt_byte_lane_r*8 +: 8]);

    // ---------------------------------------------------------------
    // Accumulator and quantization regs
    // ---------------------------------------------------------------
    reg signed [31:0] acc;
    reg signed [63:0] quant_prod;
    reg signed [31:0] quant_result;
    reg [31:0] out_word_buf;

    // ---------------------------------------------------------------
    // DEBUG: limit output to first N output pixels to avoid flooding
    // Set DBG_MAX_PIX to however many (y,x,c) triples you want to see
    // ---------------------------------------------------------------
    localparam DBG_MAX_PIX = 8;   // first 8 output values (y=0,x=0,c=0..7)
    wire dbg_en = ({out_y, out_x, out_c} < DBG_MAX_PIX);

    // ---------------------------------------------------------------
    // Combinational quantization shift logic (avoids reg-in-always)
    // ---------------------------------------------------------------
    reg signed [63:0] qs_after_shift;
    reg signed [63:0] qs_rounded;
    reg signed [63:0] qs_shifted;
    reg signed [31:0] qs_with_offset;
    reg signed [31:0] qs_clamped;
    
    always @(*) begin
        qs_after_shift  = quant_prod >>> SHIFT[out_c];
        qs_rounded      = qs_after_shift
                        + (qs_after_shift[63] ? -64'sd1073741824
                                              :  64'sd1073741824);
        qs_shifted      = qs_rounded >>> 31;
        qs_with_offset  = qs_shifted[31:0] + $signed({{24{OUTPUT_OFFSET[7]}}, OUTPUT_OFFSET});
    
        if      (qs_with_offset > 32'sd127)  qs_clamped = 32'sd127;
        else if (qs_with_offset < -32'sd128) qs_clamped = -32'sd128;
        else                                 qs_clamped = qs_with_offset;
    end

    // ---------------------------------------------------------------
    // FSM
    // ---------------------------------------------------------------
    always @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state           <= S_IDLE;
            done_o          <= 1'b0;
            out_y           <= 5'd0;
            out_x           <= 5'd0;
            out_c           <= 3'd0;
            fy              <= 4'd0;
            fx              <= 3'd0;
            acc             <= 32'sd0;
            quant_prod      <= 64'sd0;
            quant_result    <= 32'sd0;
            out_word_buf    <= 32'd0;
            eng_in_csb      <= 1'b1;
            filt_csb        <= 1'b1;
            conv_out_csb_o  <= 1'b1;
            conv_out_web_o  <= 1'b1;
            conv_out_addr_o <= 10'd0;
            conv_out_din_o  <= 32'd0;
            in_byte_lane_r  <= 2'd0;
            filt_byte_lane_r<= 2'd0;
            in_bounds_r     <= 1'b0;
        end
        else begin
            eng_in_csb     <= 1'b1;
            filt_csb       <= 1'b1;
            conv_out_csb_o <= 1'b1;
            conv_out_web_o <= 1'b1;

            case (state)

                // ------------------------------------------------
                S_IDLE: begin
                    done_o <= 1'b0;
                    if (start_i) begin
                        out_y  <= 5'd0;
                        out_x  <= 5'd0;
                        out_c  <= 3'd0;
                        fy     <= 4'd0;
                        fx     <= 3'd0;
                        acc    <= 32'sd0;
                        state  <= S_LOAD_IN;
                    end
                end

                // ------------------------------------------------
                S_LOAD_IN: begin
                    filt_csb         <= 1'b0;
                    filt_addr        <= filt_word_addr;
                    filt_byte_lane_r <= filt_byte_lane;

                    if (in_bounds) begin
                        eng_in_csb     <= 1'b0;
                        eng_in_addr    <= in_word_addr;
                        in_byte_lane_r <= in_byte_lane;
                    end
                    in_bounds_r <= in_bounds;
                    state       <= S_WAIT_BRAM;
                end

                // ------------------------------------------------
                S_WAIT_BRAM: begin
                    state <= S_MAC;
                end

                // ------------------------------------------------
                S_MAC: begin
                    if (in_bounds_r) begin
                        acc <= acc + $signed(filter_val) * $signed(input_val);

                        // DEBUG: print every MAC tap for gated pixels
                        if (dbg_en) begin
                            $display("[MAC] out(%0d,%0d,c%0d) fy=%0d fx=%0d | in_xy=(%0d,%0d) input_val=%0d filter_val=%0d | running_acc=%0d + %0d*%0d = %0d",
                                out_y, out_x, out_c,
                                fy, fx,
                                $signed(in_x_sig), $signed(in_y_sig),
                                $signed(input_val),
                                $signed(filter_val),
                                acc,
                                $signed(filter_val), $signed(input_val),
                                acc + $signed(filter_val) * $signed(input_val));
                        end
                    end else begin
                        // OOB tap — zero-padded, no accumulation
                        if (dbg_en) begin
                            $display("[MAC] out(%0d,%0d,c%0d) fy=%0d fx=%0d | OOB in_xy=(%0d,%0d) skipped (pad=0)",
                                out_y, out_x, out_c,
                                fy, fx,
                                $signed(in_x_sig), $signed(in_y_sig));
                        end
                    end

                    if (fx == FILTER_W - 1) begin
                        fx <= 3'd0;
                        if (fy == FILTER_H - 1) begin
                            fy    <= 4'd0;
                            state <= S_BIAS;
                        end else begin
                            fy    <= fy + 1'b1;
                            state <= S_LOAD_IN;
                        end
                    end else begin
                        fx    <= fx + 1'b1;
                        state <= S_LOAD_IN;
                    end
                end

                // ------------------------------------------------
                S_BIAS: begin
                    acc   <= acc + BIAS[out_c];

                    if (dbg_en) begin
                        $display("[BIAS] out(%0d,%0d,c%0d) | acc_before=%0d bias=%0d acc_after=%0d",
                            out_y, out_x, out_c,
                            acc,
                            $signed(BIAS[out_c]),
                            acc + $signed(BIAS[out_c]));
                    end

                    state <= S_QUANT_MUL;
                end

                // ------------------------------------------------
                S_QUANT_MUL: begin
                    quant_prod <= $signed(acc) * $signed(MULTIPLIER[out_c]);

                    if (dbg_en) begin
                        $display("[QMUL] out(%0d,%0d,c%0d) | acc=%0d multiplier=%0d prod=%0d",
                            out_y, out_x, out_c,
                            $signed(acc),
                            $signed(MULTIPLIER[out_c]),
                            $signed(acc) * $signed(MULTIPLIER[out_c]));
                    end

                    state <= S_QUANT_SHIFT;
                end

                // ------------------------------------------------
                S_QUANT_SHIFT: begin
                    quant_result <= qs_clamped;
                    if (dbg_en) begin
                        $display("[QSHIFT] out(%0d,%0d,c%0d) | prod=%0d after_shift=%0d rounded=%0d shifted=%0d with_offset=%0d clamped=%0d",
                            out_y, out_x, out_c,
                            $signed(quant_prod),
                            $signed(qs_after_shift),
                            $signed(qs_rounded),
                            $signed(qs_shifted),
                            $signed(qs_with_offset),
                            $signed(qs_clamped));
                    end
                    state <= S_WRITE;
                end
                
                // ------------------------------------------------
                S_WRITE: begin
                    out_word_buf[out_byte_lane*8 +: 8] <= quant_result[7:0];

                    if (dbg_en) begin
                        $display("[WRITE] out(%0d,%0d,c%0d) | final_byte=%0d (0x%02h) -> byte_lane=%0d word_addr=%0d",
                            out_y, out_x, out_c,
                            $signed(quant_result[7:0]),
                            quant_result[7:0],
                            out_byte_lane,
                            out_word_addr);
                    end

                    if (out_byte_lane == 2'b11) begin
                        conv_out_csb_o  <= 1'b0;
                        conv_out_web_o  <= 1'b0;
                        conv_out_addr_o <= out_word_addr;
                        conv_out_din_o  <= {quant_result[7:0],
                                            out_word_buf[23:16],
                                            out_word_buf[15:8],
                                            out_word_buf[7:0]};
                        if (dbg_en) begin
                            $display("[BRAM_WR] word_addr=%0d data=0x%08h (bytes: %0d %0d %0d %0d)",
                                out_word_addr,
                                {quant_result[7:0], out_word_buf[23:16], out_word_buf[15:8], out_word_buf[7:0]},
                                $signed(out_word_buf[7:0]),
                                $signed(out_word_buf[15:8]),
                                $signed(out_word_buf[23:16]),
                                $signed(quant_result[7:0]));
                        end
                    end

                    acc <= 32'sd0;
                    if (out_c == OUTPUT_C - 1) begin
                        out_c <= 3'd0;
                        if (out_x == OUTPUT_W - 1) begin
                            out_x <= 5'd0;
                            if (out_y == OUTPUT_H - 1) begin
                                state <= S_DONE;
                            end else begin
                                out_y <= out_y + 1'b1;
                                state <= S_LOAD_IN;
                            end
                        end else begin
                            out_x <= out_x + 1'b1;
                            state <= S_LOAD_IN;
                        end
                    end else begin
                        out_c <= out_c + 1'b1;
                        state <= S_LOAD_IN;
                    end
                end

                // ------------------------------------------------
                S_DONE: begin
                    done_o <= 1'b1;
                    $display("[DONE] Convolution complete.");
                    state  <= S_IDLE;
                end

                default: state <= S_IDLE;

            endcase
        end
    end

endmodule