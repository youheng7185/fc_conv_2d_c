#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#define INPUT_FILE  "../conv_filter.h"
#define OUTPUT_FILE "conv_filter.mem"

int main(void)
{
    FILE *fin;
    FILE *fout;

    int value;
    int c;
    int count = 0;

    uint8_t bytes[4];

    fin = fopen(INPUT_FILE, "r");
    if (fin == NULL) {
        printf("Error: Cannot open %s\n", INPUT_FILE);
        return 1;
    }

    fout = fopen(OUTPUT_FILE, "w");
    if (fout == NULL) {
        printf("Error: Cannot create %s\n", OUTPUT_FILE);
        fclose(fin);
        return 1;
    }

    /*
    * Find the opening '{' of conv_filter_data[].
    */
    while ((c = fgetc(fin)) != EOF) {
        if (c == '{')
            break;
    }

    if (c == EOF) {
        printf("Error: Cannot find array data\n");
        fclose(fin);
        fclose(fout);
        return 1;
    }

    /*
    * Read int8_t values until the closing '}'.
    */
    while ((c = fgetc(fin)) != EOF) {

        if (c == '}')
            break;

        /*
        * Look for the start of a number.
        */
        if (isdigit(c) || c == '-') {

            ungetc(c, fin);

            if (fscanf(fin, "%d", &value) == 1) {

                if (value < -128 || value > 127) {
                    printf("Error: Value %d is outside int8_t range\n", value);
                    fclose(fin);
                    fclose(fout);
                    return 1;
                }

                /*
                * Convert signed int8_t to its raw 8-bit representation.
                */
                bytes[count++] = (uint8_t)(int8_t)value;

                /*
                * Pack 4 bytes into one 32-bit word.
                *
                * Little endian:
                *
                * bytes[0] -> bits [7:0]
                * bytes[1] -> bits [15:8]
                * bytes[2] -> bits [23:16]
                * bytes[3] -> bits [31:24]
                */
                if (count == 4) {

                    uint32_t word;

                    word =
                        ((uint32_t)bytes[0] << 0)  |
                        ((uint32_t)bytes[1] << 8)  |
                        ((uint32_t)bytes[2] << 16) |
                        ((uint32_t)bytes[3] << 24);

                    fprintf(fout, "%08X\n", word);

                    count = 0;
                }
            }
        }
    }

    /*
    * If the number of bytes is not divisible by 4,
    * pad the remaining bytes with zero.
    */
    if (count != 0) {

        while (count < 4) {
            bytes[count++] = 0;
        }

        uint32_t word;

        word =
            ((uint32_t)bytes[0] << 0)  |
            ((uint32_t)bytes[1] << 8)  |
            ((uint32_t)bytes[2] << 16) |
            ((uint32_t)bytes[3] << 24);

        fprintf(fout, "%08X\n", word);
    }

    fclose(fin);
    fclose(fout);

    printf("Successfully generated %s\n", OUTPUT_FILE);

    return 0;

}
