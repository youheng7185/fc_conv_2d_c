module fc_top (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        start_i,
    output reg         done_o,

    output reg        bram_csb_o,
    output reg [9:0]  bram_addr_o,
    input  wire [31:0] bram_dout_i,

    output reg signed [7:0] fc_out_0,
    output reg signed [7:0] fc_out_1,
    output reg signed [7:0] fc_out_2,
    output reg signed [7:0] fc_out_3
);

    localparam FC_INPUT_SIZE    = 13'd4000;
    localparam FC_OUTPUT_OFFSET = 32'sd14;
    localparam FC_REDUCED_MULT  = 32'sd29493;
    localparam FC_TOTAL_SHIFT   = 6'd26;
    localparam FC_ACT_MIN       = -32'sd128;
    localparam FC_ACT_MAX       = 32'sd127;
    localparam FC_INPUT_OFFSET  = 32'sd128;

    // Debug controls
    localparam DBG_MAX_OUT = 2;      // watch out_c 0 and 1 only
    localparam DBG_MAX_D   = 13'd8;  // first 8 taps normal detail
    // bank boundary window: bytes 2044..2051
    localparam DBG_BANK_LO = 13'd2044;
    localparam DBG_BANK_HI = 13'd2051;
    // end of accumulation
    localparam DBG_END_LO  = 13'd3995;

    reg signed [31:0] BIAS [0:3];
    initial begin
        BIAS[0] =  32'sd427;
        BIAS[1] = -32'sd518;
        BIAS[2] = -32'sd94;
        BIAS[3] =  32'sd186;
    end

    // ---------------------------------------------------------------
    // Filter ROM
    // ---------------------------------------------------------------
    reg        filt_csb;
    reg [13:0] filt_addr;
    wire [7:0] filt_dout;

    fc_filter_rom u_fc_filter_rom (
        .clk0  (clk_i),
        .csb0  (filt_csb),
        .addr0 (filt_addr),   // full 14-bit
        .dout0 (filt_dout)
    );

    // ---------------------------------------------------------------
    // FSM states
    // ---------------------------------------------------------------
    localparam S_IDLE  = 3'd0;
    localparam S_ISSUE = 3'd1;
    localparam S_WAIT  = 3'd2;
    localparam S_WAIT2 = 3'd3;
    localparam S_MAC   = 3'd4;
    localparam S_BIAS  = 3'd5;
    localparam S_DONE  = 3'd6;

    reg [2:0] state;

    // ---------------------------------------------------------------
    // Loop counters
    // ---------------------------------------------------------------
    reg [1:0]  out_c;
    reg [12:0] d;
    reg [13:0] filt_base_addr;

    // ---------------------------------------------------------------
    // Accumulator
    // ---------------------------------------------------------------
    reg signed [31:0] acc;

    // ---------------------------------------------------------------
    // Address/lane wires — computed from d combinationally
    // ---------------------------------------------------------------
    wire [1:0]  in_byte_lane  = d[1:0];
    wire [9:0]  in_word_addr  = d[12:2];
    wire [13:0] filt_byte_addr = filt_base_addr + {1'b0, d};

    // ---------------------------------------------------------------
    // Captured values — latched in S_ISSUE, held until S_MAC
    // No shift register — just hold the value issued this transaction
    // ---------------------------------------------------------------
    reg [1:0]  captured_in_lane;    // byte lane for current d
    reg [31:0] captured_bram_dout;  // raw BRAM word, captured in S_WAIT2

    wire signed [31:0] input_val =
        $signed({{24{captured_bram_dout[captured_in_lane*8 + 7]}},
                  captured_bram_dout[captured_in_lane*8 +: 8]})
        + FC_INPUT_OFFSET;

    wire signed [31:0] filter_val = $signed(filt_dout);

    wire dbg_en = (out_c < DBG_MAX_OUT);
    wire dbg_d  = (d < DBG_MAX_D) ||
                  (d >= DBG_BANK_LO && d <= DBG_BANK_HI) ||
                  (d >= DBG_END_LO);

    // ---------------------------------------------------------------
    // FSM
    // ---------------------------------------------------------------
    always @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state              <= S_IDLE;
            done_o             <= 1'b0;
            out_c              <= 2'd0;
            d                  <= 13'd0;
            acc                <= 32'sd0;
            filt_base_addr     <= 14'd0;
            bram_csb_o         <= 1'b1;
            bram_addr_o        <= 10'd0;
            filt_csb           <= 1'b1;
            filt_addr          <= 14'd0;
            captured_in_lane   <= 2'd0;
            captured_bram_dout <= 32'd0;
            fc_out_0           <= 8'sd0;
            fc_out_1           <= 8'sd0;
            fc_out_2           <= 8'sd0;
            fc_out_3           <= 8'sd0;
        end
        else begin
            // Defaults
            bram_csb_o <= 1'b1;
            filt_csb   <= 1'b1;
            done_o     <= 1'b0;

            case (state)

                // ------------------------------------------------
                S_IDLE: begin
                    if (start_i) begin
                        out_c          <= 2'd0;
                        d              <= 13'd0;
                        acc            <= 32'sd0;
                        filt_base_addr <= 14'd0;
                        state          <= S_ISSUE;
                        $display("[FC_START] start_i asserted");
                    end
                end

                // ------------------------------------------------
                // Issue reads — both BRAM and filter ROM
                // Capture lane and d for use in S_MAC
                // ------------------------------------------------
                S_ISSUE: begin
                    bram_csb_o       <= 1'b0;
                    bram_addr_o      <= in_word_addr;
                    captured_in_lane <= in_byte_lane;   // latch now, use in S_MAC

                    filt_csb  <= 1'b0;
                    filt_addr <= filt_byte_addr;        // full 14-bit, no truncation

                    if (dbg_en && dbg_d) begin
                        $display("[FC_ISSUE] out_c=%0d d=%0d | filt_addr=%0d (bank=%0d word=%0d lane=%0d) | bram_word=%0d lane=%0d | filt_base=%0d",
                            out_c, d,
                            filt_byte_addr,
                            filt_byte_addr[13:11],
                            filt_byte_addr[10:2],
                            filt_byte_addr[1:0],
                            in_word_addr,
                            in_byte_lane,
                            filt_base_addr);
                    end

                    state <= S_WAIT;
                end

                // ------------------------------------------------
                // Cycle 1: BRAM read in progress
                // ------------------------------------------------
                S_WAIT: begin
                    state <= S_WAIT2;
                end

                // ------------------------------------------------
                // Cycle 2: BRAM output valid — capture it
                // fc_filter_rom also valid now (2-cycle latency)
                // ------------------------------------------------
                S_WAIT2: begin
                    captured_bram_dout <= bram_dout_i;  // capture raw word

                    if (dbg_en && dbg_d) begin
                        $display("[FC_WAIT2] out_c=%0d d=%0d | raw_bram=0x%08h lane=%0d raw_byte=0x%02h | filt_dout=0x%02h",
                            out_c, d,
                            bram_dout_i,
                            captured_in_lane,
                            bram_dout_i[captured_in_lane*8 +: 8],
                            filt_dout);
                    end

                    state <= S_MAC;
                end

                // ------------------------------------------------
                // MAC — use captured values, not live wires
                // ------------------------------------------------
                S_MAC: begin
                    acc <= acc + filter_val * input_val;

                    if (dbg_en && dbg_d) begin
                        $display("[FC_MAC] out_c=%0d d=%0d | input_val=%0d filter_val=%0d | acc=%0d -> %0d",
                            out_c, d,
                            $signed(input_val),
                            $signed(filter_val),
                            acc,
                            acc + filter_val * input_val);
                    end

                    if (d == FC_INPUT_SIZE - 1) begin
                        d     <= 13'd0;
                        state <= S_BIAS;
                    end else begin
                        d     <= d + 1'b1;
                        state <= S_ISSUE;
                    end
                end

                // ------------------------------------------------
                S_BIAS: begin
                    reg signed [31:0] biased;
                    reg signed [63:0] prod;
                    reg signed [63:0] rounded;
                    reg signed [31:0] quant;
                    reg signed [31:0] with_offset;
                    reg signed [31:0] clamped;

                    biased      = acc + BIAS[out_c];
                    prod        = $signed(biased) * $signed(FC_REDUCED_MULT);
                    rounded     = prod + 64'sd33554432;
                    quant       = rounded >>> FC_TOTAL_SHIFT;
                    with_offset = quant + FC_OUTPUT_OFFSET;

                    if      (with_offset > FC_ACT_MAX) clamped = FC_ACT_MAX;
                    else if (with_offset < FC_ACT_MIN) clamped = FC_ACT_MIN;
                    else                               clamped = with_offset;

                    $display("[FC_BIAS]  out_c=%0d | acc_before=%0d bias=%0d biased=%0d",
                        out_c, acc, $signed(BIAS[out_c]), $signed(biased));
                    $display("[FC_QMUL]  out_c=%0d | biased=%0d reduced_mult=%0d prod=%0d",
                        out_c, $signed(biased), FC_REDUCED_MULT, $signed(prod));
                    $display("[FC_QSHIFT] out_c=%0d | prod=%0d rounded=%0d shift=%0d quant=%0d with_offset=%0d clamped=%0d",
                        out_c, $signed(prod), $signed(rounded),
                        FC_TOTAL_SHIFT, $signed(quant),
                        $signed(with_offset), $signed(clamped));
                    $display("[FC_WRITE] out_c=%0d | final=%0d (0x%02h)",
                        out_c, $signed(clamped), clamped[7:0]);

                    case (out_c)
                        2'd0: fc_out_0 <= clamped[7:0];
                        2'd1: fc_out_1 <= clamped[7:0];
                        2'd2: fc_out_2 <= clamped[7:0];
                        2'd3: fc_out_3 <= clamped[7:0];
                    endcase

                    acc            <= 32'sd0;
                    filt_base_addr <= filt_base_addr + 14'd4000;

                    if (out_c == 2'd3) begin
                        state <= S_DONE;
                    end else begin
                        out_c <= out_c + 1'b1;
                        state <= S_ISSUE;
                    end
                end

                // ------------------------------------------------
                S_DONE: begin
                    done_o <= 1'b1;
                    $display("[FC_DONE] fc_out = %0d %0d %0d %0d",
                        $signed(fc_out_0), $signed(fc_out_1),
                        $signed(fc_out_2), $signed(fc_out_3));
                    state <= S_IDLE;
                end

                default: state <= S_IDLE;

            endcase
        end
    end

endmodule