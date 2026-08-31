#include "Vmicro_speech_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "../conv_input_data.h"
#include "../fc_output_data.h"

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
    // Wait for done_o (conv ~976k + fc ~64k cycles)
    // -------------------------------------------------------
    const uint64_t TIMEOUT = 6500000ULL;
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
    // Sample FC output registers directly — they are live wires
    // -------------------------------------------------------
    std::cout << "[TB] Verifying FC output...\n";

    int8_t got[4] = {
        (int8_t)dut->fc_out_0,
        (int8_t)dut->fc_out_1,
        (int8_t)dut->fc_out_2,
        (int8_t)dut->fc_out_3,
    };

    int errors = 0;
    for (int i = 0; i < 4; i++) {
        int8_t exp = fc_output_no[i];
        if (got[i] != exp) {
            std::cout << "[MISMATCH] fc_out_" << i
                      << "  exp=" << (int)exp
                      << "  got=" << (int)got[i] << "\n";
            errors++;
        } else {
            std::cout << "[OK]       fc_out_" << i
                      << "  val=" << (int)got[i] << "\n";
        }
    }

    if (errors == 0) {
        std::cout << "[TB] All 4 FC outputs match. PASS\n";
    } else {
        std::cout << "[TB] " << errors << " FC output mismatches. FAIL\n";
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