# Makefile for MDIO Verilator testbench

VERILATOR_FLAGS = -Wall --trace \
    -Wno-fatal \
    -Wno-UNUSED \
    -Wno-UNDRIVEN \
    -Wno-PINMISSING

# ==================================================
# Original conv_top master
# ==================================================

conv_top_TOP = conv_top
conv_top_TB  = tb_conv_top.cpp
conv_top_OBJ = obj_dir_conv_top

conv_top_SRC = ../hdl/conv_top.v \
				../hdl/conv_filter_rom.v \
				../hdl/sky130_sram_2kbyte_1rw1r_32x512_8.v \
				../hdl/conv_filter_rom.v

$(conv_top_OBJ)/V$(conv_top_TOP).mk: $(conv_top_SRC) $(conv_top_TB)
	verilator $(VERILATOR_FLAGS) \
		--Mdir $(conv_top_OBJ) \
		--cc $(conv_top_SRC) \
		--exe $(conv_top_TB)

build_conv_top: $(conv_top_OBJ)/V$(conv_top_TOP).mk
	make -j -C $(conv_top_OBJ) -f V$(conv_top_TOP).mk V$(conv_top_TOP)

run_conv_top: build_conv_top
	./$(conv_top_OBJ)/V$(conv_top_TOP)

# ==================================================
# Original micro_speech_top master
# ==================================================

micro_speech_top_TOP = micro_speech_top
micro_speech_top_TB  = tb_micro_speech_top.cpp
micro_speech_top_OBJ = obj_dir_micro_speech_top

micro_speech_top_SRC = ../hdl/micro_speech_top.v \
				../hdl/conv_top.v \
				../hdl/conv_filter_rom.v \
				../hdl/sky130_sram_2kbyte_1rw1r_32x512_8.v \
				../hdl/conv_filter_rom.v \
				../hdl/conv_output_bram.v

$(micro_speech_top_OBJ)/V$(micro_speech_top_TOP).mk: $(micro_speech_top_SRC) $(micro_speech_top_TB)
	verilator $(VERILATOR_FLAGS) \
		--Mdir $(micro_speech_top_OBJ) \
		--cc $(micro_speech_top_SRC) \
		--exe $(micro_speech_top_TB)

build_ms_top: $(micro_speech_top_OBJ)/V$(micro_speech_top_TOP).mk
	make -j -C $(micro_speech_top_OBJ) -f V$(micro_speech_top_TOP).mk V$(micro_speech_top_TOP)

run_ms_top: build_ms_top
	./$(micro_speech_top_OBJ)/V$(micro_speech_top_TOP)

# ==================================================
# Default
# ==================================================

all: run_conv_top

clean:
	rm -rf obj_dir_conv_top obj_dir_micro_speech_top
	rm -f *.vcd *.o *.d *.exe

.PHONY: all \
	build_conv_top run_conv_top \
	build_ms_top run_ms_top \
	clean