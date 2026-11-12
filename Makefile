CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2

.PHONY: all run test clean

all: rsh

rsh: src/shell.c
	$(CC) $(CFLAGS) $< -o $@

run: rsh
	./rsh

test: rsh
	./tests/test.sh

clean:
	rm -f rsh
