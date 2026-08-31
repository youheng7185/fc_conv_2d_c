#include "Vmicro_speech_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "../conv_input_data.h"
#include "../fc_output_data.h"

vluint64_t sim_time = 0;

void tick(Vmicro_speech_top *dut, VerilatedVcdC *tfp) {
    dut->clk_i = 0; dut->eval(); tfp->dump(sim_time++);
    dut->clk_i = 1; dut->eval(); tfp->dump(sim_time++);
}

void delay(Vmicro_speech_top *dut, VerilatedVcdC *tfp, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) tick(dut, tfp);
}

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
// Run one full inference and verify against expected output
// Returns number of mismatches
// -------------------------------------------------------
int run_test(Vmicro_speech_top *dut, VerilatedVcdC *tfp,
             const char        *name,
             const int8_t      *input_data,
             const int8_t      *expected)
{
    std::cout << "\n[TB] ========== " << name << " ==========\n";

    // Reset between tests
    dut->rst_ni  = 0;
    dut->start_i = 0;
    dut->tb_csb_i        = 1;
    dut->tb_web_i        = 1;
    dut->tb_addr_i       = 0;
    dut->tb_din_i        = 0;
    delay(dut, tfp, 5);
    dut->rst_ni = 1;
    delay(dut, tfp, 5);

    // Load input BRAM
    constexpr int INPUT_BYTES = 1960;
    constexpr int INPUT_WORDS = (INPUT_BYTES + 3) / 4;   // 490

    uint8_t bram_bytes[INPUT_WORDS * 4];
    memset(bram_bytes, 0, sizeof(bram_bytes));
    for (int i = 0; i < INPUT_BYTES; i++)
        bram_bytes[i] = (uint8_t)input_data[i];

    for (int w = 0; w < INPUT_WORDS; w++) {
        uint32_t word = ((uint32_t)bram_bytes[w*4 + 0])        |
                        ((uint32_t)bram_bytes[w*4 + 1] <<  8)  |
                        ((uint32_t)bram_bytes[w*4 + 2] << 16)  |
                        ((uint32_t)bram_bytes[w*4 + 3] << 24);
        bram_write(dut, tfp, (uint16_t)w, word);
    }
    std::cout << "[TB] Input BRAM loaded (" << INPUT_WORDS << " words).\n";

    // Assert start_i
    dut->start_i = 1;
    tick(dut, tfp);
    dut->start_i = 0;

    // Wait for done_o
    const uint64_t TIMEOUT = 6500000ULL;
    uint64_t cycles = 0;
    while (!dut->done_o && cycles < TIMEOUT) {
        tick(dut, tfp);
        cycles++;
        if (cycles % 500000 == 0)
            std::cout << "[TB] Running... cycle " << cycles << "\n";
    }

    if (!dut->done_o) {
        std::cout << "[TB] TIMEOUT — done_o never asserted. FAIL\n";
        return 4;   // all 4 outputs failed
    }
    std::cout << "[TB] done_o after " << cycles << " cycles.\n";

    // Extra tick so done_o is visible in waveform
    tick(dut, tfp);

    // Verify
    int8_t got[4] = {
        (int8_t)dut->fc_out_0,
        (int8_t)dut->fc_out_1,
        (int8_t)dut->fc_out_2,
        (int8_t)dut->fc_out_3,
    };

    int errors = 0;
    for (int i = 0; i < 4; i++) {
        int8_t exp = expected[i];
        if (got[i] != exp) {
            std::cout << "[MISMATCH] " << name
                      << " fc_out_" << i
                      << "  exp=" << (int)exp
                      << "  got=" << (int)got[i] << "\n";
            errors++;
        } else {
            std::cout << "[OK]       " << name
                      << " fc_out_" << i
                      << "  val=" << (int)got[i] << "\n";
        }
    }

    if (errors == 0)
        std::cout << "[TB] " << name << " — PASS\n";
    else
        std::cout << "[TB] " << name << " — FAIL (" << errors << " mismatches)\n";

    return errors;
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

    int total_errors = 0;

    total_errors += run_test(dut, tfp, "no",
                             conv2d_input_no,      fc_output_no);

    total_errors += run_test(dut, tfp, "yes",
                             conv2d_input_yes,     fc_output_yes);

    total_errors += run_test(dut, tfp, "silence",
                             conv2d_input_silence, fc_output_silence);

    total_errors += run_test(dut, tfp, "noise",
                             conv2d_input_noise,   fc_output_noise);

    std::cout << "\n[TB] ========== SUMMARY ==========\n";
    if (total_errors == 0)
        std::cout << "[TB] All tests PASSED.\n";
    else
        std::cout << "[TB] " << total_errors << " total mismatches. FAILED.\n";

    dut->final();
    tfp->close();
    delete dut;
    delete tfp;
    return (total_errors == 0) ? 0 : 1;
}