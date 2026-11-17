# Build every generated workload header into a bare-metal binary.
#   python3 scripts/gen_workloads.py      (from the repo root, first)
#   make -C workloads                     (inside the cy container)
#
# Uses printf/exit/_init from the toolchain's riscv-tests (syscalls.c) and our
# own multi-hart crt.S + link.ld.

RT      ?= /workspace/chipyard/toolchains/riscv-tools/riscv-tests
CC      ?= riscv64-unknown-elf-gcc
ifeq ($(CC),cc)
CC      := riscv64-unknown-elf-gcc
endif

CFLAGS  = -O2 -mcmodel=medany -static -std=gnu99 -ffast-math -fno-common \
          -fno-builtin-printf -fno-tree-loop-distribute-patterns \
          -march=rv64gc -mabi=lp64d -Wall -Wno-unused-function \
          -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable \
          -Iruntime -Itasks -I$(RT)/env -I$(RT)/benchmarks/common
LDFLAGS = -static -nostdlib -nostartfiles -lm -lgcc -T runtime/link.ld -Wl,--no-warn-rwx-segments

HEADERS := $(wildcard build/*/*.h)
BINS    := $(HEADERS:.h=.riscv)
DEPS    := runtime/crt.S runtime/link.ld runtime/runtime.h tasks/tasks.c tasks/tasks.h harness/harness.c

all: check $(BINS)
	@echo "Built $(words $(BINS)) binaries."

check:
	@test -f $(RT)/benchmarks/common/syscalls.c || \
	  (echo "riscv-tests not found at RT=$(RT); pass RT=<path to riscv-tests>"; exit 1)
	@test -n "$(HEADERS)" || \
	  (echo "No headers in workloads/build. Run: python3 scripts/gen_workloads.py"; exit 1)

build/%.riscv: build/%.h $(DEPS)
	@echo "  CC  $@"
	@$(CC) $(CFLAGS) -DNHARTS=$$(sed -n 's/^#define WL_NHARTS //p' $<) \
	  -DWORKLOAD_HEADER='"$(abspath $<)"' -o $@ \
	  runtime/crt.S harness/harness.c tasks/tasks.c $(RT)/benchmarks/common/syscalls.c $(LDFLAGS)

clean:
	rm -f $(BINS)

.PHONY: all check clean
