#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#define INPUT_FILE   "../fc_filter.h"
#define NUM_BANKS    8
#define WORDS_PER_BANK 512  // 512 words × 4 bytes = 2KB per bank

int main(void)
{
    FILE *fin;
    FILE *fout[NUM_BANKS];
    int value;
    int count = 0;
    int word_count = 0;

    uint8_t bytes[4];

    fin = fopen(INPUT_FILE, "r");
    if (fin == NULL) {
        printf("Error: Cannot open %s\n", INPUT_FILE);
        return 1;
    }

    // Open 8 output files
    for (int i = 0; i < NUM_BANKS; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "fc_filter_%d.mem", i);
        fout[i] = fopen(filename, "w");
        if (fout[i] == NULL) {
            printf("Error: Cannot create %s\n", filename);
            for (int j = 0; j < i; j++) fclose(fout[j]);
            fclose(fin);
            return 1;
        }
    }

    /*
     * Find the opening '{' of the array.
     */
    int c;
    while ((c = fgetc(fin)) != EOF) {
        if (c == '{')
            break;
    }

    if (c == EOF) {
        printf("Error: Cannot find array data\n");
        fclose(fin);
        for (int i = 0; i < NUM_BANKS; i++) fclose(fout[i]);
        return 1;
    }

    /*
     * Read signed decimal numbers until '}'.
     */
    while (1) {
        c = fgetc(fin);

        if (c == EOF)
            break;

        if (c == '}')
            break;

        if (isdigit(c) || c == '-') {

            ungetc(c, fin);

            if (fscanf(fin, "%d", &value) == 1) {

                if (value < -128 || value > 127) {
                    printf("Error: Value out of int8 range: %d\n", value);
                    fclose(fin);
                    for (int i = 0; i < NUM_BANKS; i++) fclose(fout[i]);
                    return 1;
                }

                bytes[count++] = (uint8_t)(int8_t)value;

                if (count == 4) {

                    uint32_t word =
                        ((uint32_t)bytes[0] << 0)  |
                        ((uint32_t)bytes[1] << 8)  |
                        ((uint32_t)bytes[2] << 16) |
                        ((uint32_t)bytes[3] << 24);

                    // word_count / WORDS_PER_BANK gives which bank (0..7)
                    // word_count % WORDS_PER_BANK gives line within bank
                    int bank = word_count / WORDS_PER_BANK;

                    if (bank >= NUM_BANKS) {
                        printf("Error: More words than expected (max %d)\n",
                               NUM_BANKS * WORDS_PER_BANK);
                        fclose(fin);
                        for (int i = 0; i < NUM_BANKS; i++) fclose(fout[i]);
                        return 1;
                    }

                    fprintf(fout[bank], "%08X\n", word);
                    word_count++;
                    count = 0;
                }
            }
        }
    }

    /*
     * Handle remaining bytes if total isn't divisible by 4.
     * Pad with zero.
     */
    if (count != 0) {
        while (count < 4)
            bytes[count++] = 0;

        uint32_t word =
            ((uint32_t)bytes[0] << 0)  |
            ((uint32_t)bytes[1] << 8)  |
            ((uint32_t)bytes[2] << 16) |
            ((uint32_t)bytes[3] << 24);

        int bank = word_count / WORDS_PER_BANK;
        fprintf(fout[bank], "%08X\n", word);
        word_count++;
    }

    fclose(fin);
    for (int i = 0; i < NUM_BANKS; i++)
        fclose(fout[i]);

    printf("Successfully generated %d bank files (%d words total)\n",
           NUM_BANKS, word_count);
    printf("Expected: 4000 words across 8 banks of 500 words each\n");

    // Sanity check - 16000 bytes = 4000 words, 500 words per bank
    // Note: last bank only has 500 words, not 512 - addresses 500..511 unused
    if (word_count != 4000)
        printf("Warning: Expected 4000 words, got %d\n", word_count);

    return 0;
}