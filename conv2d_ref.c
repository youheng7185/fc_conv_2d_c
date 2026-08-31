#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "conv_input_data.h"
#include "conv_output_data.h"
#include "conv_filter.h"

#define STRIDE_W 2
#define STRIDE_H 2
#define DILATION_W 1
#define DILATION_H 1
#define PAD_W 3
#define PAD_H 4

#define DEPTH_MULTIPLIER 8

#define INPUT_OFFSET   128
#define OUTPUT_OFFSET -128

#define ACT_MIN -128
#define ACT_MAX  127

#define BATCHES        1
#define INPUT_H        49
#define INPUT_W        40
#define INPUT_C        1

#define FILTER_H       10
#define FILTER_W       8
#define OUTPUT_C       8

#define OUTPUT_H       25
#define OUTPUT_W       20

// How many output pixels to debug (same idea as DBG_MAX_PIX in HDL)
// 8 = first spatial position (y=0,x=0) all 8 channels
#define DBG_MAX_PIX    8

static const int32_t output_multiplier[8] = {
  1653229999, 1516545207, 2000799311, 1159928266,
  1498403863, 1285645282, 2146175029, 1756589032
};

static const int32_t output_shift[8] = {
  -10, -12, -10, -10, -10, -10, -10, -10
};

static const int32_t bias_data[8] = {
  -374, 169, -48, 208, 82, 6, -1201, -694
};

extern const int8_t conv_filter_data[640];

static inline int input_offset_nhwc(int b, int y, int x, int c) {
  return ((b * INPUT_H + y) * INPUT_W + x) * INPUT_C + c;
}

static inline int filter_offset(int fy, int fx, int oc) {
  return (fy * FILTER_W + fx) * OUTPUT_C + oc;
}

static inline int output_offset_nhwc(int b, int y, int x, int c) {
  return ((b * OUTPUT_H + y) * OUTPUT_W + x) * OUTPUT_C + c;
}

static inline int32_t MultiplyByQuantizedMultiplier(
    int32_t x, int32_t multiplier, int shift) {

  int64_t prod = (int64_t)x * (int64_t)multiplier;

  if (shift < 0) {
    prod = prod >> (-shift);
  } else {
    prod = prod << shift;
  }

  prod += (prod >= 0) ? (1ll << 30) : -(1ll << 30);
  prod >>= 31;

  return (int32_t)prod;
}

void depthwise_conv_per_channel(
    const int8_t* input_data,
    int8_t* output_data,
    int enable_debug) {

  int pix_count = 0;

  for (int b = 0; b < BATCHES; b++) {
    for (int out_y = 0; out_y < OUTPUT_H; out_y++) {
      for (int out_x = 0; out_x < OUTPUT_W; out_x++) {
        for (int in_c = 0; in_c < INPUT_C; in_c++) {
          for (int m = 0; m < DEPTH_MULTIPLIER; m++) {

            int out_c = in_c * DEPTH_MULTIPLIER + m;
            int dbg = enable_debug && (pix_count < DBG_MAX_PIX);

            int in_x_origin = out_x * STRIDE_W - PAD_W;
            int in_y_origin = out_y * STRIDE_H - PAD_H;

            int32_t acc = 0;

            for (int fy = 0; fy < FILTER_H; fy++) {
              for (int fx = 0; fx < FILTER_W; fx++) {

                int in_x = in_x_origin + fx * DILATION_W;
                int in_y = in_y_origin + fy * DILATION_H;

                if ((unsigned)in_x < INPUT_W &&
                    (unsigned)in_y < INPUT_H) {

                  int32_t input_val =
                      input_data[input_offset_nhwc(b, in_y, in_x, in_c)];

                  int32_t filter_val =
                      conv_filter_data[filter_offset(fy, fx, out_c)];

                  int32_t contrib = filter_val * (input_val + INPUT_OFFSET);
                  acc += contrib;

                  if (dbg) {
                    printf("[MAC] out(%d,%d,c%d) fy=%d fx=%d | in_xy=(%d,%d)"
                           " input_val=%d filter_val=%d | running_acc=%d + %d*%d = %d\n",
                           out_y, out_x, out_c,
                           fy, fx,
                           in_x, in_y,
                           (int32_t)(input_val + INPUT_OFFSET),
                           filter_val,
                           acc - contrib,
                           filter_val, (int32_t)(input_val + INPUT_OFFSET),
                           acc);
                  }

                } else {
                  if (dbg) {
                    printf("[MAC] out(%d,%d,c%d) fy=%d fx=%d | OOB in_xy=(%d,%d)"
                           " skipped (pad=0)\n",
                           out_y, out_x, out_c,
                           fy, fx,
                           in_x, in_y);
                  }
                }
              }
            }

            // Bias
            int32_t acc_before_bias = acc;
            acc += bias_data[out_c];

            if (dbg) {
              printf("[BIAS] out(%d,%d,c%d) | acc_before=%d bias=%d acc_after=%d\n",
                     out_y, out_x, out_c,
                     acc_before_bias,
                     bias_data[out_c],
                     acc);
            }

            // Quantized multiply
            int32_t multiplier = output_multiplier[out_c];
            int     shift      = output_shift[out_c];
            int64_t prod       = (int64_t)acc * (int64_t)multiplier;

            if (dbg) {
              printf("[QMUL] out(%d,%d,c%d) | acc=%d multiplier=%d prod=%lld\n",
                     out_y, out_x, out_c,
                     acc, multiplier, (long long)prod);
            }

            // Shift
            int64_t prod_shifted = prod;
            if (shift < 0) {
              prod_shifted = prod_shifted >> (-shift);
            } else {
              prod_shifted = prod_shifted << shift;
            }

            int64_t rounded = prod_shifted
                              + (prod_shifted >= 0 ? (1ll << 30) : -(1ll << 30));
            int64_t shifted = rounded >> 31;

            int32_t with_offset = (int32_t)shifted + OUTPUT_OFFSET;

            int32_t clamped;
            if      (with_offset > ACT_MAX) clamped = ACT_MAX;
            else if (with_offset < ACT_MIN) clamped = ACT_MIN;
            else                            clamped = with_offset;

            if (dbg) {
              printf("[QSHIFT] out(%d,%d,c%d) | prod=%lld rounded=%lld"
                     " shift=%d shifted=%lld with_offset=%d clamped=%d\n",
                     out_y, out_x, out_c,
                     (long long)prod,
                     (long long)rounded,
                     31 + (-shift),   // total shift to match HDL display
                     (long long)shifted,
                     with_offset,
                     clamped);

              printf("[WRITE] out(%d,%d,c%d) | final_byte=%d (0x%02x)"
                     " -> byte_lane=%d word_addr=%d\n",
                     out_y, out_x, out_c,
                     clamped,
                     (uint8_t)(int8_t)clamped,
                     out_c % 4,
                     output_offset_nhwc(b, out_y, out_x, out_c) / 4);
            }

            output_data[output_offset_nhwc(b, out_y, out_x, out_c)] =
                (int8_t)clamped;

            pix_count++;
          }
        }
      }
    }
  }
}

int main() {
    int8_t output_cnn[4000];

    printf("=== conv2d_input_no ===\n");
    depthwise_conv_per_channel(conv2d_input_no, output_cnn, 1);
    int diff = memcmp(output_cnn, conv2d_output_no, 4000);
    printf("diff no: %d\n\n", diff);

    printf("=== conv2d_input_yes ===\n");
    depthwise_conv_per_channel(conv2d_input_yes, output_cnn, 0);
    diff = memcmp(output_cnn, conv2d_output_yes, 4000);
    printf("diff yes: %d\n\n", diff);

    printf("=== conv2d_input_noise ===\n");
    depthwise_conv_per_channel(conv2d_input_noise, output_cnn, 0);
    diff = memcmp(output_cnn, conv2d_output_noise, 4000);
    printf("diff noise: %d\n\n", diff);

    printf("=== conv2d_input_silence ===\n");
    depthwise_conv_per_channel(conv2d_input_silence, output_cnn, 0);
    diff = memcmp(output_cnn, conv2d_output_silence, 4000);
    printf("diff silence: %d\n\n", diff);

    return 0;
}