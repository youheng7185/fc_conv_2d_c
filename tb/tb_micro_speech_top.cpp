#include "Vmicro_speech_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "../conv_input_data.h"
#include "../conv_output_data.h"

// -------------------------------------------------------
// Sim time
// -------------------------------------------------------
vluint64_t sim_time = 0;

void tick(Vmicro_speech_top *dut, VerilatedVcdC *tfp) {
    dut->clk_i = 0;
    dut->eval();
    tfp->dump(sim_time++);
    dut->clk_i = 1;
    dut->eval();
    tfp->dump(sim_time++);
}

void delay(Vmicro_speech_top *dut, VerilatedVcdC *tfp, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        tick(dut, tfp);
}

// -------------------------------------------------------
// Write one 32-bit word into input BRAM via tb_ port
// -------------------------------------------------------
void bram_write(Vmicro_speech_top *dut, VerilatedVcdC *tfp,
                uint16_t addr, uint32_t data) {
    dut->tb_csb_i  = 0;
    dut->tb_web_i  = 0;
    dut->tb_addr_i = addr & 0x1FF;
    dut->tb_din_i  = data;
    tick(dut, tfp);
    dut->tb_csb_i  = 1;
    dut->tb_web_i  = 1;
    dut->tb_addr_i = 0;
    dut->tb_din_i  = 0;
}

// -------------------------------------------------------
// Read one 32-bit word from conv_output_bram via external port
// 1-cycle read latency (registered BRAM)
// -------------------------------------------------------
uint32_t bram_read(Vmicro_speech_top *dut, VerilatedVcdC *tfp, uint16_t addr) {
    dut->out_bram_csb_i  = 0;
    dut->out_bram_addr_i = addr & 0x3FF;
    tick(dut, tfp);                 // latch address, BRAM registers output
    dut->out_bram_csb_i  = 1;
    dut->out_bram_addr_i = 0;
    dut->clk_i = 0; dut->eval();   // sample dout on falling edge (stable)
    return dut->out_bram_dout_o;
}

// -------------------------------------------------------
// MAIN
// -------------------------------------------------------
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vmicro_speech_top *dut = new Vmicro_speech_top;
    VerilatedVcdC     *tfp = new VerilatedVcdC;

    Verilated::traceEverOn(true);
    dut->trace(tfp, 99);
    tfp->open("micro_speech_top_waveform.vcd");

    // -------------------------------------------------------
    // Reset
    // -------------------------------------------------------
    dut->rst_ni          = 0;
    dut->start_i         = 0;
    dut->tb_csb_i        = 1;
    dut->tb_web_i        = 1;
    dut->tb_addr_i       = 0;
    dut->tb_din_i        = 0;
    dut->out_bram_csb_i  = 1;
    dut->out_bram_addr_i = 0;
    delay(dut, tfp, 5);
    dut->rst_ni = 1;
    delay(dut, tfp, 5);

    // -------------------------------------------------------
    // Load input BRAM
    // conv2d_input_no: 1960 bytes (40 x 49), packed as 490 words
    // -------------------------------------------------------
    std::cout << "[TB] Loading input BRAM...\n";

    constexpr int INPUT_BYTES = 1960;
    constexpr int INPUT_WORDS = (INPUT_BYTES + 3) / 4;   // 490

    uint8_t bram_bytes[INPUT_WORDS * 4];
    memset(bram_bytes, 0, sizeof(bram_bytes));
    for (int i = 0; i < INPUT_BYTES; i++)
        bram_bytes[i] = (uint8_t)conv2d_input_no[i];

    for (int w = 0; w < INPUT_WORDS; w++) {
        uint32_t word = ((uint32_t)bram_bytes[w*4 + 0])       |
                        ((uint32_t)bram_bytes[w*4 + 1] <<  8) |
                        ((uint32_t)bram_bytes[w*4 + 2] << 16) |
                        ((uint32_t)bram_bytes[w*4 + 3] << 24);
        bram_write(dut, tfp, (uint16_t)w, word);
    }

    std::cout << "[TB] BRAM loaded (" << INPUT_WORDS << " words).\n";

    // -------------------------------------------------------
    // Assert start_i for one cycle
    // -------------------------------------------------------
    std::cout << "[TB] Asserting start_i...\n";
    dut->start_i = 1;
    tick(dut, tfp);
    dut->start_i = 0;

    // -------------------------------------------------------
    // Wait for done_o
    // -------------------------------------------------------
    const uint64_t TIMEOUT = 5000000ULL;
    uint64_t cycles = 0;
    while (!dut->done_o && cycles < TIMEOUT) {
        tick(dut, tfp);
        cycles++;
        if (cycles % 100000 == 0)
            std::cout << "[TB] Running... cycle " << cycles << "\n";
    }

    if (!dut->done_o) {
        std::cout << "[TB] TIMEOUT — done_o never asserted. FAIL\n";
        dut->final(); tfp->close(); delete dut; delete tfp;
        return 1;
    }
    std::cout << "[TB] done_o asserted after " << cycles << " cycles.\n";

    // Extra tick so done_o is visible in waveform
    tick(dut, tfp);

    // -------------------------------------------------------
    // Read back and verify output BRAM
    // conv2d_output_no: 4000 bytes (25 x 20 x 8)
    // Packed as 1000 words of 32 bits, little-endian
    // -------------------------------------------------------
    std::cout << "[TB] Verifying output...\n";

    constexpr int OUTPUT_BYTES = 4000;
    constexpr int OUTPUT_WORDS = (OUTPUT_BYTES + 3) / 4;  // 1000

    int errors   = 0;
    int mismatches = 0;

    for (int w = 0; w < OUTPUT_WORDS; w++) {
        uint32_t got = bram_read(dut, tfp, (uint16_t)w);

        // Build expected word from reference array (little-endian)
        int base = w * 4;
        uint32_t exp = 0;
        for (int b = 0; b < 4; b++) {
            int idx = base + b;
            uint8_t byte_val = (idx < OUTPUT_BYTES)
                               ? (uint8_t)conv2d_output_no[idx]
                               : 0;
            exp |= ((uint32_t)byte_val << (b * 8));
        }

        if (got != exp) {
            mismatches++;
            // Print per-byte breakdown for first 20 mismatches
            if (mismatches <= 20) {
                std::cout << "[MISMATCH] word " << w
                          << "  exp=0x" << std::hex << exp
                          << "  got=0x" << got << std::dec << "\n";
                // Per-byte detail
                for (int b = 0; b < 4; b++) {
                    int idx = base + b;
                    int8_t e = (idx < OUTPUT_BYTES) ? conv2d_output_no[idx] : 0;
                    int8_t g = (int8_t)((got >> (b*8)) & 0xFF);
                    if (e != g) {
                        std::cout << "  byte[" << idx << "] (y="
                                  << idx/160 << " x=" << (idx%160)/8
                                  << " c=" << idx%8
                                  << ")  exp=" << (int)e
                                  << "  got=" << (int)g << "\n";
                    }
                }
            }
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "[TB] All " << OUTPUT_WORDS << " words match. PASS\n";
    } else {
        std::cout << "[TB] " << errors << " word mismatches out of "
                  << OUTPUT_WORDS << ". FAIL\n";
    }

    // -------------------------------------------------------
    // Finish
    // -------------------------------------------------------
    int ret = (errors == 0) ? 0 : 1;
    dut->final();
    tfp->close();
    delete dut;
    delete tfp;
    return ret;
}