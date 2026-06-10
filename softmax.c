#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include "fc_output_data.h"
#include <string.h>

#define INPUT_BETA_MULTIPLIER   1575942400
#define INPUT_BETA_LEFT_SHIFT   23
#define DIFF_MIN                (-248)
#define DEPTH                   4
#define SCALED_DIFF_FRAC_BITS   26
#define ACCUM_FRAC_BITS         19

// gemmlowp SRDHM: round half away from zero (used in exp, one_over, etc.)
static inline int32_t SaturatingRoundingDoublingHighMul(int32_t a, int32_t b) {
    int64_t ab = (int64_t)a * (int64_t)b;
    int64_t nudge = (ab >= 0) ? (1LL << 30) : (1LL - (1LL << 30));
    int32_t result = (int32_t)((ab + nudge) >> 31);
    if (a == INT32_MIN && b == INT32_MIN) result = INT32_MAX;
    return result;
}

// tflite SRDHM: always nudge +(1<<30), used in MultiplyByQuantizedMultiplierGreaterThanOne
static inline int32_t SaturatingRoundingDoublingHighMulTflite(int32_t a, int32_t b) {
    int64_t ab = (int64_t)a * (int64_t)b;
    int64_t nudge = 1LL << 30;
    int32_t result = (int32_t)((ab + nudge) >> 31);
    if (a == INT32_MIN && b == INT32_MIN) result = INT32_MAX;
    return result;
}

static inline int32_t RoundingDivideByPOT(int32_t x, int exponent) {
    int32_t mask = (1 << exponent) - 1;
    int32_t rem  = x & mask;
    int32_t thr  = (mask >> 1) + ((x < 0) ? 1 : 0);
    return (x >> exponent) + (rem > thr ? 1 : 0);
}

static inline int32_t MultiplyByQuantizedMultiplierGreaterThanOne(
        int32_t x, int32_t multiplier, int left_shift) {
    return SaturatingRoundingDoublingHighMulTflite(x << left_shift, multiplier);
}

static inline int32_t exp_on_interval(int32_t a) {
    const int32_t c1o3 = 715827883;
    const int32_t c1o4 = 536870912;
    const int32_t c1o5 = 429496730;
    const int32_t c1o6 = 357913942;
    const int32_t c1o7 = 306783379;
    const int32_t c1o8 = 268435456;
    const int32_t ONE  = INT32_MAX;
    int32_t x;
    x = SaturatingRoundingDoublingHighMul(a, c1o8);
    x = SaturatingRoundingDoublingHighMul(a, x + c1o7);
    x = SaturatingRoundingDoublingHighMul(a, x + c1o6);
    x = SaturatingRoundingDoublingHighMul(a, x + c1o5);
    x = SaturatingRoundingDoublingHighMul(a, x + c1o4);
    x = SaturatingRoundingDoublingHighMul(a, x + c1o3);
    x = SaturatingRoundingDoublingHighMul(a, x + ONE);
    return x + ONE;
}

static inline int32_t exp_on_negative_values_q5_26(int32_t a) {
    const int kFractionalBits = 26;
    const int32_t kOneQuarter = 1 << (kFractionalBits - 2);
    int32_t mask = kOneQuarter - 1;
    int32_t a_mod_quarter_minus_one_quarter = (a & mask) - kOneQuarter;
    int32_t a_rescaled = a_mod_quarter_minus_one_quarter << (31 - kFractionalBits);
    int32_t result = exp_on_interval(a_rescaled);
    int32_t remainder = a_mod_quarter_minus_one_quarter - a;

#define BARREL(shift_amount, multiplier)                                      \
    if (remainder & (1 << (shift_amount))) {                                  \
        result = SaturatingRoundingDoublingHighMul(result, (multiplier));     \
    }
    BARREL(24, 1672461947)
    BARREL(25, 1302514674)
    BARREL(26,  790015084)
    BARREL(27,  290630308)
    BARREL(28,   39332535)
    BARREL(29,     720401)
    BARREL(30,        242)
#undef BARREL

    if (a == 0) result = INT32_MAX;
    return result;
}

static inline int CountLeadingZeros(uint32_t x) {
    if (x == 0) return 32;
    int n = 0;
    if ((x & 0xFFFF0000u) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000u) == 0) { n +=  8; x <<=  8; }
    if ((x & 0xF0000000u) == 0) { n +=  4; x <<=  4; }
    if ((x & 0xC0000000u) == 0) { n +=  2; x <<=  2; }
    if ((x & 0x80000000u) == 0) { n +=  1; }
    return n;
}

// RoundingHalfSum: (a + b) rounding average, matches vrhaddq_s32 / gemmlowp RoundingHalfSum
static inline int32_t RoundingHalfSum(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a + (int64_t)b + 1) >> 1);
}

// one_over_one_plus_x_for_x_in_0_1
// Matches gemmlowp exactly: Newton-Raphson in F2 (Q2.29), returns Q0.31
// F0 = Q0.31 (frac=31), F2 = Q2.29 (frac=29)
// SRDHM(a,b) used for F2*F2 → F2 (both are 32-bit, result >>31 with rounding)
// F2::One() = 1<<29  (1.0 in Q2.29)
// Rescale<2>(x*y): x*y is Q2.29 * Q2.29 via SRDHM = Q2.29, stays Q2.29
// ExactMulByPot<-1>(x): right shift by 1, Q2.29 → effectively x/2
// Rescale<0>(ExactMulByPot<-1>(x)): Q2.29 >> (29-31) ... no:
//   Rescale from F2→F0 = shift by (29-31) = left shift 2, but after halving:
//   ExactMulByPot<-1> on Q2.29 raw = raw>>1, giving Q2.29 with value halved
//   Rescale<0> from Q2.29 to Q0.31 = left shift by (31-29)=2
//   Net: (raw >> 1) << 2 = raw << 1
static inline int32_t one_over_one_plus_x_for_x_in_0_1(int32_t a) {
    // a is Q0.31 (F0), One() = INT32_MAX
    const int32_t F0_ONE = INT32_MAX;          // 1.0 in Q0.31
    const int32_t F2_ONE = (1 << 29);          // 1.0 in Q2.29

    // half_denominator = RoundingHalfSum(a, F0::One())  → Q0.31
    int32_t half_denominator = RoundingHalfSum(a, F0_ONE);

    // Constants in F2 (Q2.29):
    const int32_t k48over17    =  1515870810;  // 48/17 in Q2.29
    const int32_t kneg32over17 = -1010580540;  // -32/17 in Q2.29

    // x = 48/17 + half_denominator * (-32/17)
    // half_denominator is Q0.31, kneg32over17 is Q2.29
    // SRDHM(Q0.31, Q2.29) gives Q2.29 (product >>31 = (Q0.31 * Q2.29) >> 31 = Q2.29)
    int32_t x = k48over17 + SaturatingRoundingDoublingHighMul(half_denominator, kneg32over17);

    // 3 Newton-Raphson steps, all in F2 (Q2.29)
    for (int i = 0; i < 3; i++) {
        // half_denominator_times_x = half_denominator * x
        // SRDHM(Q0.31, Q2.29) → Q2.29
        int32_t half_denom_times_x = SaturatingRoundingDoublingHighMul(half_denominator, x);
        // one_minus = F2::One() - half_denominator_times_x
        int32_t one_minus = F2_ONE - half_denom_times_x;
        // x * one_minus: SRDHM(Q2.29, Q2.29) → Q2.29 ... but gemmlowp does Rescale<2>(x * one_minus)
        // x * one_minus via SRDHM = Q2.29 (already F2), Rescale<2> is identity (F2→F2)
        int32_t correction = SaturatingRoundingDoublingHighMul(x, one_minus);
        x = x + correction;
    }

    // return Rescale<0>(ExactMulByPot<-1>(x))
    // ExactMulByPot<-1>(x) = x >> 1  (exact, no rounding needed per gemmlowp)
    // Rescale<0> from F2(Q2.29) to F0(Q0.31) = left shift by (31-29) = 2
    // Combined: (x >> 1) << 2 = x << 1
    return x << 1;
}

static inline int32_t GetReciprocal(int32_t x, int x_integer_digits,
                                    int* num_bits_over_unit) {
    int headroom_plus_one = CountLeadingZeros((uint32_t)x);
    *num_bits_over_unit = x_integer_digits - headroom_plus_one;
    int32_t shifted_sum_minus_one =
        (int32_t)(((uint32_t)x << headroom_plus_one) - ((uint32_t)1 << 31));
    printf("  GetReciprocal: headroom_plus_one=%d\n", headroom_plus_one);
    printf("  GetReciprocal: num_bits_over_unit=%d\n", *num_bits_over_unit);
    printf("  GetReciprocal: shifted_sum_minus_one=0x%x\n", (uint32_t)shifted_sum_minus_one);
    int32_t shifted_scale = one_over_one_plus_x_for_x_in_0_1(shifted_sum_minus_one);
    printf("  GetReciprocal: shifted_scale=0x%x\n", (uint32_t)shifted_scale);
    return shifted_scale;
}

void softmax_fixed(const int8_t* input_data, int8_t* output_data) {
    const int32_t INT8_MIN_VAL = -128;
    const int32_t INT8_MAX_VAL =  127;

    // Step 1: find max
    int8_t max_val = input_data[0];
    for (int c = 1; c < DEPTH; c++)
        if (input_data[c] > max_val) max_val = input_data[c];
    printf("max_val: %d\n", max_val);

    // Step 2: sum of exps
    int32_t sum_of_exps = 0;
    for (int c = 0; c < DEPTH; c++) {
        int32_t input_diff = (int32_t)input_data[c] - (int32_t)max_val;
        printf("[c=%d] input_diff: %d\n", c, input_diff);
        if (input_diff >= DIFF_MIN) {
            int32_t input_diff_rescaled = MultiplyByQuantizedMultiplierGreaterThanOne(
                input_diff, INPUT_BETA_MULTIPLIER, INPUT_BETA_LEFT_SHIFT);
            printf("  input_diff_rescaled: %d (0x%x)\n", input_diff_rescaled, (uint32_t)input_diff_rescaled);

            int32_t exp_val = exp_on_negative_values_q5_26(input_diff_rescaled);
            printf("  exp_in_0 Q0.31: 0x%x\n", (uint32_t)exp_val);

            int32_t exp_accum = RoundingDivideByPOT(exp_val, 31 - ACCUM_FRAC_BITS);
            printf("  exp_accum Q12.19: 0x%x\n", (uint32_t)exp_accum);

            sum_of_exps += exp_accum;
            printf("  sum_of_exps so far: 0x%x\n", (uint32_t)sum_of_exps);
        }
    }
    printf("sum of exp: 0x%x\n", (uint32_t)sum_of_exps);

    // Step 3: reciprocal
    int num_bits_over_unit;
    int32_t shifted_scale = GetReciprocal(sum_of_exps, 12, &num_bits_over_unit);
    printf("num_bits_over_unit: %d\n", num_bits_over_unit);
    printf("shifted_scale: 0x%x\n", (uint32_t)shifted_scale);

    const int exponent = num_bits_over_unit + 31 - 8;
    printf("exponent: %d\n", exponent);

    // Step 4: output
    for (int c = 0; c < DEPTH; c++) {
        int32_t input_diff = (int32_t)input_data[c] - (int32_t)max_val;
        printf("[c=%d] input_diff: %d\n", c, input_diff);
        if (input_diff >= DIFF_MIN) {
            int32_t input_diff_rescaled = MultiplyByQuantizedMultiplierGreaterThanOne(
                input_diff, INPUT_BETA_MULTIPLIER, INPUT_BETA_LEFT_SHIFT);
            printf("  input_diff_rescaled: %d\n", input_diff_rescaled);

            int32_t exp_in_0 = exp_on_negative_values_q5_26(input_diff_rescaled);
            printf("  exp_in_0: 0x%x\n", (uint32_t)exp_in_0);

            int32_t product = SaturatingRoundingDoublingHighMul(shifted_scale, exp_in_0);
            printf("  shifted_scale * exp_in_0: 0x%x\n", (uint32_t)product);

            int32_t unsat_output = RoundingDivideByPOT(product, exponent);
            printf("  unsat_output: %d\n", unsat_output);

            int32_t shifted_output = unsat_output + INT8_MIN_VAL;
            printf("  shifted_output: %d\n", shifted_output);

            if (shifted_output > INT8_MAX_VAL) shifted_output = INT8_MAX_VAL;
            if (shifted_output < INT8_MIN_VAL) shifted_output = INT8_MIN_VAL;
            output_data[c] = (int8_t)shifted_output;
        } else {
            output_data[c] = (int8_t)INT8_MIN_VAL;
        }
        printf("  output data: %d\n", output_data[c]);
    }
}

const char* labels[4] = {"silence", "unknown", "yes", "no"};

// the values are slightly off compare to tflite official testbench
const int8_t expected_no[4]      = {-128, -114, -128,  110};
const int8_t expected_yes[4]     = {-128, -128,  121, -128};
const int8_t expected_silence[4] = { -39,  -67,  -67,  -77};
const int8_t expected_noise[4]   = { 115, -125, -126, -125};

void PrintSoftmaxOutput(const int8_t* output) {
    for (uint8_t i = 0; i < 4; i++) {
        float pct = (output[i] + 128) / 256.0f * 100.0f;
        printf("  %.4f %s\n", pct, labels[i]);
    }
}

int main() {
    int8_t input_no[4]       = {-61,  37, -13,  68};
    int8_t input_yes[4]      = {-50,  -4, 121,  -4};
    int8_t input_silence[4]  = { 18,  14,  14,  12};
    int8_t input_noise[4]    = { 55,   7,   2,   8};
    int8_t output[4] = {0};

    printf("--- no ---\n");
    softmax_fixed(input_no, output);
    PrintSoftmaxOutput(output);
    int diff = memcmp(output, expected_no, 4);
    printf("diff no: %d\n", diff);

    printf("--- yes ---\n");
    softmax_fixed(input_yes, output);
    PrintSoftmaxOutput(output);
    diff = memcmp(output, expected_yes, 4);
    printf("diff yes: %d\n", diff);

    printf("--- silence ---\n");
    softmax_fixed(input_silence, output);
    PrintSoftmaxOutput(output);
    diff = memcmp(output, expected_silence, 4);
    printf("diff silence: %d\n", diff);

    printf("--- noise ---\n");
    softmax_fixed(input_noise, output);
    PrintSoftmaxOutput(output);
    diff = memcmp(output, expected_noise, 4);
    printf("diff noise: %d\n", diff);

    return 0;
}