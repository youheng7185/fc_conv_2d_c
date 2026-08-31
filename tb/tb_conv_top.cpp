#include "Vconv_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <cstring>

// // Full 1960-byte input array (int8, sign-extended to uint8 for BRAM packing)
// static const int8_t conv2d_input_no[1960] = {
//     103, 78, 64, 76, 75, 54, 53, 67, 77, 60, 56, 70, 76, 71, 68, 58,
//     74, 32, 23, -2, -18, 11, 13, 15, 9, 20, 5, -7, -18, -2, -10, -18,
//     // -- paste the rest of your array here --
//     // (fill remaining 1928 values)
// };
#include "../conv_input_data.h"

vluint64_t sim_time = 0;

void tick(Vconv_top *dut, VerilatedVcdC *tfp) {
    dut->clk_i = 0;
    dut->eval();
    tfp->dump(sim_time++);
    dut->clk_i = 1;
    dut->eval();
    tfp->dump(sim_time++);
}

void delay(Vconv_top *dut, VerilatedVcdC *tfp, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        tick(dut, tfp);
}

// Write one 32-bit word into the input BRAM via the tb_ port
void bram_write(Vconv_top *dut, VerilatedVcdC *tfp,
                uint16_t addr, uint32_t data) {
    dut->tb_csb_i  = 0;   // chip select active-low
    dut->tb_web_i  = 0;   // write enable active-low
    dut->tb_addr_i = addr & 0x1FF;
    dut->tb_din_i  = data;
    tick(dut, tfp);
    // deassert
    dut->tb_csb_i  = 1;
    dut->tb_web_i  = 1;
    dut->tb_addr_i = 0;
    dut->tb_din_i  = 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Vconv_top *dut = new Vconv_top;
    VerilatedVcdC *tfp = new VerilatedVcdC;

    Verilated::traceEverOn(true);
    dut->trace(tfp, 99);
    tfp->open("conv_top_waveform.vcd");

    // -------------------------------------------------------
    // Reset
    // -------------------------------------------------------
    dut->rst_ni    = 0;
    dut->start_i   = 0;
    dut->tb_csb_i  = 1;
    dut->tb_web_i  = 1;
    dut->tb_addr_i = 0;
    dut->tb_din_i  = 0;
    delay(dut, tfp, 5);
    dut->rst_ni = 1;
    delay(dut, tfp, 5);

    // -------------------------------------------------------
    // Load input BRAM
    // conv2d_input_no is 1960 bytes (40*49 = 1960)
    // BRAM is word-addressed (32-bit words), so 490 words
    // Pack 4 bytes per word, little-endian
    // -------------------------------------------------------
    std::cout << "[TB] Loading input BRAM...\n";

    // Pad to next 4-byte boundary
    constexpr int INPUT_BYTES = 1960;
    constexpr int INPUT_WORDS = (INPUT_BYTES + 3) / 4;  // 490

    uint8_t bram_bytes[INPUT_WORDS * 4];
    memset(bram_bytes, 0, sizeof(bram_bytes));
    for (int i = 0; i < INPUT_BYTES; i++)
        bram_bytes[i] = (uint8_t)conv2d_input_no[i];

    for (int w = 0; w < INPUT_WORDS; w++) {
        uint32_t word = ((uint32_t)bram_bytes[w*4 + 0])        |
                        ((uint32_t)bram_bytes[w*4 + 1] << 8)   |
                        ((uint32_t)bram_bytes[w*4 + 2] << 16)  |
                        ((uint32_t)bram_bytes[w*4 + 3] << 24);
        bram_write(dut, tfp, (uint16_t)w, word);
    }

    std::cout << "[TB] BRAM loaded (" << INPUT_WORDS << " words). Asserting start_i...\n";

    // -------------------------------------------------------
    // Assert start_i for one cycle
    // -------------------------------------------------------
    dut->start_i = 1;
    tick(dut, tfp);
    dut->start_i = 0;

    // -------------------------------------------------------
    // Wait for done_o
    // Worst-case cycles: 25*20*8 * (8*10 * 4 + overhead) ~ a few million
    // Use a generous timeout
    // -------------------------------------------------------
    const uint64_t TIMEOUT = 5000000ULL;
    uint64_t cycles = 0;
    while (!dut->done_o && cycles < TIMEOUT) {
        tick(dut, tfp);
        cycles++;
        if (cycles % 100000 == 0)
            std::cout << "[TB] Still running... cycle " << cycles << "\n";
    }

    if (dut->done_o) {
        std::cout << "[TB] done_o asserted after " << cycles << " cycles. PASS\n";
    } else {
        std::cout << "[TB] TIMEOUT after " << cycles << " cycles. FAIL\n";
    }

    // One extra tick to capture done in waveform
    tick(dut, tfp);

    // -------------------------------------------------------
    // Finish
    // -------------------------------------------------------
    int ret = dut->done_o ? 0 : 1;   // capture BEFORE final()

    dut->final();
    tfp->close();
    delete dut;
    delete tfp;
    return 0;
}