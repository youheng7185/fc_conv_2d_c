#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fc_filter.h"
#include "conv_output_data.h"
#include "fc_output_data.h"

#define FC_INPUT_SIZE      4000
#define FC_OUTPUT_SIZE     4
#define FC_INPUT_OFFSET    128
#define FC_OUTPUT_OFFSET   14
#define FC_ACT_MIN        -128
#define FC_ACT_MAX         127

// Match HDL: only print first N neurons and first M taps
#define DBG_MAX_OUT  4
#define DBG_MAX_D    8

static const int32_t fc_bias_data[FC_OUTPUT_SIZE] = {427, -518, -94, 186};

// Matching the HDL's precomputed reduced_multiplier path exactly
// reduced_multiplier = (1932201031 + (1<<15)) >> 16 = 29493
// total_shift = 15 - (-11) = 26
#define FC_REDUCED_MULT  29493
#define FC_TOTAL_SHIFT   26

void FullyConnectedDebug(const int8_t* input_data, int8_t* output_data) {
    for (int out_c = 0; out_c < FC_OUTPUT_SIZE; out_c++) {

        int dbg = (out_c < DBG_MAX_OUT);
        int32_t acc = 0;

        for (int d = 0; d < FC_INPUT_SIZE; d++) {
            int32_t input_val  = (int32_t)input_data[d] + FC_INPUT_OFFSET;
            int32_t filter_val = (int32_t)fc_filter_data[out_c * FC_INPUT_SIZE + d];
            int32_t contrib    = filter_val * input_val;

            if (dbg && d < DBG_MAX_D) {
                printf("[FC_MAC] out_c=%d d=%d | input_val=%d filter_val=%d"
                       " | running_acc=%d + %d*%d = %d\n",
                       out_c, d,
                       input_val, filter_val,
                       acc,
                       filter_val, input_val,
                       acc + contrib);
            }

            acc += contrib;
        }

        // Bias
        int32_t biased = acc + fc_bias_data[out_c];

        if (dbg) {
            printf("[FC_BIAS]  out_c=%d | acc_before=%d bias=%d biased=%d\n",
                   out_c, acc, fc_bias_data[out_c], biased);
        }

        // Quantized multiply — reduced_multiplier path (matches HDL)
        int64_t prod    = (int64_t)biased * (int64_t)FC_REDUCED_MULT;
        int64_t rounded = prod + (1LL << (FC_TOTAL_SHIFT - 1));  // + 1<<25
        int32_t quant   = (int32_t)(rounded >> FC_TOTAL_SHIFT);

        if (dbg) {
            printf("[FC_QMUL]  out_c=%d | biased=%d reduced_mult=%d prod=%lld\n",
                   out_c, biased, FC_REDUCED_MULT, (long long)prod);
            printf("[FC_QSHIFT] out_c=%d | prod=%lld rounded=%lld shift=%d"
                   " quant=%d with_offset=%d clamped=%d\n",
                   out_c,
                   (long long)prod,
                   (long long)rounded,
                   FC_TOTAL_SHIFT,
                   quant,
                   quant + FC_OUTPUT_OFFSET,
                   (quant + FC_OUTPUT_OFFSET > FC_ACT_MAX) ? FC_ACT_MAX :
                   (quant + FC_OUTPUT_OFFSET < FC_ACT_MIN) ? FC_ACT_MIN :
                    quant + FC_OUTPUT_OFFSET);
        }

        // Output offset + clamp
        int32_t with_offset = quant + FC_OUTPUT_OFFSET;
        int32_t clamped;
        if      (with_offset > FC_ACT_MAX) clamped = FC_ACT_MAX;
        else if (with_offset < FC_ACT_MIN) clamped = FC_ACT_MIN;
        else                               clamped = with_offset;

        if (dbg) {
            printf("[FC_WRITE] out_c=%d | final=%d (0x%02x)\n",
                   out_c, clamped, (uint8_t)(int8_t)clamped);
        }

        output_data[out_c] = (int8_t)clamped;
    }

    printf("[FC_DONE] fc_out = %d %d %d %d\n",
           output_data[0], output_data[1],
           output_data[2], output_data[3]);
}

int main() {
    int8_t fc_out[FC_OUTPUT_SIZE];

    printf("=== conv2d_input_no ===\n");
    FullyConnectedDebug(conv2d_output_no, fc_out);
    int diff = memcmp(fc_out, fc_output_no, 4);
    printf("diff no: %d\n\n", diff);

    return 0;
}