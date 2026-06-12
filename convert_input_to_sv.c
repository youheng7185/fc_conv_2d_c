#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "conv_input_data.h"

/**
 * Generates a SystemVerilog localparam array declaration from an int8 array.
 *
 * @param fp        Output file pointer (e.g. stdout or a .sv file)
 * @param arr       Pointer to the int8 data
 * @param len       Number of elements
 * @param arr_name  Name to use for the SV array (e.g. "conv2d_input")
 * @param cols      How many values to print per line (e.g. 16)
 */
void generate_sv_array(FILE *fp, const int8_t *arr, int len,
                       const char *arr_name, int cols)
{
    fprintf(fp, "localparam int N_%s = %d;\n", arr_name, len);
    fprintf(fp, "localparam shortint signed %s [0:%d] = '{\n", arr_name, len - 1);

    for (int i = 0; i < len; i++) {
        if (i % cols == 0)
            fprintf(fp, "    ");                          // indent

        fprintf(fp, "%4d", arr[i]);

        if (i < len - 1)
            fprintf(fp, ",");

        if ((i % cols == cols - 1) || (i == len - 1))    // end of row
            fprintf(fp, "\n");
    }

    fprintf(fp, "};\n");
}

int main(void)
{
    FILE *fp = fopen("conv2d_input_pkg.sv", "w");
    if (!fp) { perror("fopen"); return 1; }

    // optional: wrap in a SV package header
    fprintf(fp, "package input_data_pkg;\n\n");

    generate_sv_array(fp, conv2d_input_no, 1960, "conv2d_input_no", 16);
    generate_sv_array(fp, conv2d_input_yes, 1960, "conv2d_input_yes", 16);
    generate_sv_array(fp, conv2d_input_silence, 1960, "conv2d_input_silence", 16);
    generate_sv_array(fp, conv2d_input_noise, 1960, "conv2d_input_noise", 16);

    fprintf(fp, "\nendpackage\n");
    fclose(fp);
    return 0;
}