NASM ?= nasm
PYTHON ?= python3
QEMU ?= qemu-system-i386
OUT ?= build
ROM ?= $(OUT)/diagnostic.sfc
STEPS ?= 10000000

CORE := $(wildcard src/*.inc)
ASFLAGS := -f bin -Isrc/
ifneq ($(IPL),)
ASFLAGS += '-DAPU_IPL="$(IPL)"'
endif

.PHONY: all linux macos windows dos boot386 test test-ppu test-apu test-boot clean

all: linux macos windows dos boot386

linux: $(OUT)/snes-linux
macos: $(OUT)/snes-macos
windows: $(OUT)/snes-win.exe
dos: $(OUT)/snesdos.exe
boot386: $(OUT)/snes-386.img

$(OUT):
	mkdir -p "$@"

$(OUT)/diagnostic.sfc: tests/diagnostic.asm | $(OUT)
	$(NASM) -f bin "$<" -o "$@"

$(OUT)/snes-linux: src/host_linux.asm $(CORE) $(IPL) | $(OUT)
	$(NASM) $(ASFLAGS) "$<" -o "$@"
	chmod +x "$@"

$(OUT)/snes-macos: src/host_macos.asm src/host_linux.asm $(CORE) $(IPL) | $(OUT)
	$(NASM) $(ASFLAGS) "$<" -o "$@"
	chmod +x "$@"

$(OUT)/snes-win.exe: src/host_win.asm $(CORE) $(IPL) | $(OUT)
	$(NASM) $(ASFLAGS) "$<" -o "$@"

$(OUT)/snesdos.exe: src/host_dos.asm $(CORE) $(IPL) | $(OUT)
	$(NASM) $(ASFLAGS) "$<" -o "$@"

# Repack even when ROM or STEPS changes between invocations.
.PHONY: $(OUT)/snes-386.img
$(OUT)/snes-386.img: src/host_boot386.asm $(CORE) $(ROM) $(IPL) | $(OUT)
	$(NASM) $(ASFLAGS) -DSTEPS=$(STEPS) '-DROM_FILE="$(ROM)"' "$<" -o "$@"

ifeq ($(shell uname -s),Darwin)
HOST := $(OUT)/snes-macos
RUNNER := arch -x86_64
PPU_TEST_FLAGS := -DHOST_MAC=1
else
HOST := $(OUT)/snes-linux
endif

test: $(HOST) $(OUT)/diagnostic.sfc test-ppu
	$(PYTHON) tests/test_diagnostic.py "$(HOST)" "$(OUT)/diagnostic.sfc" "$(OUT)/diagnostic.ppm" $(RUNNER)

$(OUT)/ppu-test: tests/ppu_test.asm src/ppu.inc | $(OUT)
	$(NASM) $(ASFLAGS) $(PPU_TEST_FLAGS) "$<" -o "$@"
	chmod +x "$@"

test-ppu: $(OUT)/ppu-test
	$(RUNNER) "$(OUT)/ppu-test"

$(OUT)/test-ipl.bin: tests/apu_ipl.asm | $(OUT)
	$(NASM) -f bin "$<" -o "$@"

$(OUT)/diagnostic-apu.sfc: tests/diagnostic.asm | $(OUT)
	$(NASM) -f bin -DTEST_APU "$<" -o "$@"

test-apu: $(OUT)/test-ipl.bin $(OUT)/diagnostic-apu.sfc
	$(MAKE) OUT="$(OUT)/apu" IPL="$(OUT)/test-ipl.bin" $(if $(RUNNER),macos,linux)
	$(PYTHON) tests/test_diagnostic.py "$(OUT)/apu/$(notdir $(HOST))" "$(OUT)/diagnostic-apu.sfc" "$(OUT)/diagnostic-apu.ppm" $(RUNNER)

test-boot: $(OUT)/snes-386.img $(HOST)
	$(PYTHON) tests/test_boot.py "$(QEMU)" "$(OUT)/snes-386.img" "$(HOST)" "$(ROM)" "$(STEPS)" $(RUNNER)

clean:
	rm -f "$(OUT)/diagnostic.sfc" "$(OUT)/diagnostic.ppm" "$(OUT)/diagnostic.trace" "$(OUT)/ppu-test" "$(OUT)/snesdos.exe" "$(OUT)/snes-win.exe" "$(OUT)/snes-linux" "$(OUT)/snes-macos" "$(OUT)/snes-386.img"
