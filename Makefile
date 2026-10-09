CC ?= gcc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
all: build/basic build/contention
build:
	mkdir -p build
build/basic: src/basic.c | build
	$(CC) $(CFLAGS) -pthread $< -o $@
build/contention: src/contention.c | build
	$(CC) $(CFLAGS) -pthread $< -o $@
check: all
	python3 scripts/check.py
clean:
	rm -rf build
.PHONY: all check clean
