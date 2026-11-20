# CrashSafeKV build. Needs gcc + GNU make (MSYS2 UCRT64) on PATH.
#
#   make           build/libcskv.a, build/cskv.exe, build/cskv_corrupt.exe
#   make test      unit tests (Unity), then pytest integration tests if present
#   make leakcheck unit tests rebuilt with counting malloc/free: fails on any leak
#   make gui       start the Tkinter GUI
#   make memcheck  run the unit tests under Dr. Memory (where Dr. Memory works)
#   make clean     remove build/

# GNU make predefines CC=cc; use gcc unless the user overrides it.
ifeq ($(origin CC),default)
CC = gcc
endif

BUILD    := build
OBJ      := $(BUILD)/obj
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g -Iinclude -MMD -MP

LIB      := $(BUILD)/libcskv.a
CLI      := $(BUILD)/cskv.exe
CORRUPT  := $(BUILD)/cskv_corrupt.exe
TESTS    := $(BUILD)/cskv_tests.exe

LIB_OBJ  := $(patsubst src/%.c,$(OBJ)/src/%.o,$(wildcard src/*.c))
CLI_OBJ  := $(patsubst cli/%.c,$(OBJ)/cli/%.o,$(wildcard cli/*.c))
TEST_OBJ := $(patsubst tests/%.c,$(OBJ)/tests/%.o,$(wildcard tests/test_*.c)) \
            $(OBJ)/tests/unity.o

# Leak-check build: same engine + tests, every allocation counted.
MEMTEST  := $(BUILD)/cskv_memtest.exe
MT_OBJ   := $(patsubst %.c,$(OBJ)/mt/%.o,$(wildcard src/*.c) $(wildcard tests/test_*.c)) \
            $(OBJ)/mt/memtrack.o $(OBJ)/tests/unity.o

.PHONY: all test leakcheck gui memcheck clean

all: $(LIB) $(CLI) $(CORRUPT)

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

$(CLI): $(CLI_OBJ) $(LIB)
	$(CC) $^ -o $@

$(CORRUPT): $(OBJ)/tools/cskv_corrupt.o $(LIB)
	$(CC) $^ -o $@

$(TESTS): $(TEST_OBJ) $(LIB)
	$(CC) $^ -o $@

# Engine, CLI and test sources: strict warnings.
$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests/unity -c $< -o $@

$(MEMTEST): $(MT_OBJ)
	$(CC) $^ -o $@

$(OBJ)/mt/memtrack.o: tests/memtrack.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/mt/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests/unity -DCSKV_MEMTRACK -include tests/memtrack.h -c $< -o $@

# Vendored Unity is third-party code: build it without -Werror.
$(OBJ)/tests/unity.o: tests/unity/unity.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -g -c $< -o $@

test: $(TESTS) $(CLI) $(CORRUPT)
	./$(TESTS)
	@if [ -d tests/integration ]; then python -m pytest -q tests/integration; fi

gui: all
	python gui/app.py

leakcheck: $(MEMTEST)
	./$(MEMTEST)

memcheck: $(TESTS)
	@mkdir -p $(BUILD)/drmemory
	drmemory -batch -logdir $(BUILD)/drmemory -- $(TESTS)

clean:
	rm -rf $(BUILD)

# Auto-generated header dependencies (from -MMD).
-include $(wildcard $(OBJ)/*/*.d $(OBJ)/*/*/*.d)
