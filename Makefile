CC      := gcc
CFLAGS  := -Wall -Wextra -O2

TARGETS := conv2d fc softmax convert

all: $(TARGETS)

conv2d: conv2d.c
	$(CC) $(CFLAGS) -o $@ $<

fc: fc.c
	$(CC) $(CFLAGS) -o $@ $<

softmax: softmax.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

convert: convert_input_to_sv.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: all clean
