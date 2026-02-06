#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fc_filter.h"
#include "conv_output_data.h"
#include "fc_output_data.h"
#include <string.h>

#define FC_INPUT_SIZE 4000
#define FC_OUTPUT_SIZE 4

// Hardcoded quantization and activation parameters
#define FC_INPUT_OFFSET 128
#define FC_FILTER_OFFSET 0
#define FC_OUTPUT_OFFSET 14
#define FC_OUTPUT_MULTIPLIER 1932201031
#define FC_OUTPUT_SHIFT -11
#define FC_ACT_MIN -128
#define FC_ACT_MAX 127

// Bias (hardcoded)
static const int32_t fc_bias_data[FC_OUTPUT_SIZE] = {427, -518, -94, 186};

// Multiply by quantized multiplier helper
int32_t MultiplyByQuantizedMultiplier(int64_t x, int32_t quantized_multiplier, int shift) {
  int32_t reduced_multiplier = (quantized_multiplier < 0x7FFF0000)
                                   ? ((quantized_multiplier + (1 << 15)) >> 16)
                                   : 0x7FFF;
  int total_shift = 15 - shift;
  x = (x * (int64_t)reduced_multiplier) + ((int64_t)1 << (total_shift - 1));
  int32_t result = x >> total_shift;
  return result;
}

// Pure-C FullyConnected layer
void FullyConnectedC(const int8_t* input_data, int8_t* output_data) {
    for (int out_c = 0; out_c < FC_OUTPUT_SIZE; ++out_c) {
        int32_t acc = 0;
        for (int d = 0; d < FC_INPUT_SIZE; ++d) {
            int32_t input_val = (int32_t)input_data[d];
            int32_t filter_val = (int32_t)fc_filter_data[out_c * FC_INPUT_SIZE + d];
            acc += (filter_val + FC_FILTER_OFFSET) * (input_val + FC_INPUT_OFFSET);
        }
        // Add bias
        acc += fc_bias_data[out_c];

        // Fixed-point multiply (TFLite style)
        acc = MultiplyByQuantizedMultiplier(acc, FC_OUTPUT_MULTIPLIER, FC_OUTPUT_SHIFT);

        // Add output offset and clamp
        acc += FC_OUTPUT_OFFSET;
        if (acc > FC_ACT_MAX) acc = FC_ACT_MAX;
        if (acc < FC_ACT_MIN) acc = FC_ACT_MIN;

        output_data[out_c] = (int8_t)acc;
    }
}

// Optional: print output (Softmax or raw scores)
void PrintFCOutput(const int8_t* output_data) {
    printf("FullyConnected output:\n");
    for (int i = 0; i < FC_OUTPUT_SIZE; i++) {
        printf("  %d: %d\n", i, output_data[i]);
    }
}

// Example usage
int main() {

    int8_t fc_out[FC_OUTPUT_SIZE];

    FullyConnectedC(conv2d_output_no, fc_out);
    PrintFCOutput(fc_out);
    int diff = memcmp(fc_out, fc_output_no, 4);

    printf("diff no: %d\n", diff);

    FullyConnectedC(conv2d_output_yes, fc_out);
    PrintFCOutput(fc_out);
    diff = memcmp(fc_out, fc_output_yes, 4);

    printf("diff yes: %d\n", diff);

    FullyConnectedC(conv2d_output_silence, fc_out);
    PrintFCOutput(fc_out);
    diff = memcmp(fc_out, fc_output_silence, 4);

    printf("diff silence: %d\n", diff);

    FullyConnectedC(conv2d_output_noise, fc_out);
    PrintFCOutput(fc_out);
    diff = memcmp(fc_out, fc_output_noise, 4);

    printf("diff noise: %d\n", diff);
    
    return 0;
}
