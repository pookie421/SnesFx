# SnesFx architecture

SnesFx is a headless SNES emulator prototype written in NASM. One machine core
assembles into 32-bit protected-mode and 64-bit x86 binaries. The runners load
a cartridge, execute to guest stop or an instruction budget, and report a
deterministic result. The [roadmap](../ROADMAP.md) tracks the work needed for
real-time play and hardware conformance.

## Core and host boundary

[The machine core](../src/core.inc) owns guest time, memory, device state, and
bus ordering. The common entry points are `core_reset` and `core_step`.
A runner supplies `rom_buffer`, `rom_size`, and `ROM_CAP`, reserves the
`CORE_STATE` block, and calls the core using its internal register convention.
The includes use common 32-bit registers and state addresses. The stack
helpers and jump-table pointer sizes adapt to the host's assembly mode.

Runners deal with file I/O, executable layout, and output. Linux and macOS
use system calls, Windows imports system APIs, DOS uses a DPMI host, and the
BIOS image uses firmware to load the payload before switching to protected
mode. NASM emits these binaries directly. Make drives the build, and Python
runs the integration checks.

| Runner | Host boundary | Outputs |
| --- | --- | --- |
| [Linux](../src/host_linux.asm) | Static x86-64 ELF and Linux system calls | Text summary, optional PPM and binary trace |
| [macOS](../src/host_macos.asm) | x86-64 Mach-O and macOS system calls | Text summary, optional PPM and binary trace |
| [Windows](../src/host_win.asm) | x86-64 PE with Win32 imports | Text summary and optional PPM |
| [DOS](../src/host_dos.asm) | MZ loader, 32-bit DPMI, conventional-memory DOS I/O buffer | Text summary |
| [386 BIOS floppy](../src/host_boot386.asm) | BIOS floppy read, A20 setup, and 32-bit protected mode | VGA text summary and emulator debug port |

The BIOS image embeds a cartridge in the read-only payload. Its fixed floppy
layout allows up to 512 KiB for the binary and cartridge together. The
integration test configures a 486-class emulated PC with 2 MiB of RAM. Native
and DOS file-loading builds allow up to 8 MiB plus a 512-byte copier header.

## Clock, bus, and cartridge

The core tracks a 64-bit SNES master-tick counter, horizontal and vertical
positions, field parity, and the next scanline event. The clock fast path
advances directly when no scheduled boundary or horizontal IRQ check is due.
The slower path handles modeled WRAM refresh, HBlank, VBlank, IRQ, NMI, and
field transitions. A PAL cartridge header selects a 312-line baseline,
while the NTSC baseline uses 262 lines and models the shorter odd-field
scanline. Refresh and interrupt phase timing still need hardware verification.
[Scheduler and bus](../src/core.inc)

The 65C816 dispatch table selects handlers for all opcode values. Handlers
use distinct 8- and 16-bit data paths, byte-ordered accesses, and helpers for
wrapping, stack semantics, and interrupt entry. CPU bus helpers apply the
address-dependent wait class and maintain the CPU data latch. DMA and HDMA
use the same untimed address decoder and add transfer timing in their own
handlers. General DMA starts between CPU instructions, and queued HDMA
work is serviced at instruction or general-DMA byte boundaries.
[65C816 interpreter](../src/cpu.inc) · [DMA and HDMA](../src/dma.inc)

The cartridge loader checks header candidates for LoROM, HiROM, and ExHiROM,
then maps mirrored regions on the 24-bit bus. A valid reset vector and
supported map byte are required. The checksum pair helps score competing
header candidates. Cart SRAM is capped at 64 KiB and its contents live for the
session. Enhanced cartridge types require their own implementation.
[Cartridge loader and memory map](../src/core.inc)

The I/O handlers expose the math unit, controller latch and automatic-poll
state, DMA registers, WRAM port, interrupt control, and APU/PPU ports. The
runners leave input in its default state. Host keyboard or controller
integration remains a roadmap item. [CPU-side I/O](../src/io.inc)

## Picture processing

The PPU owns 64 KiB of VRAM, 512 bytes of CGRAM, and 544 bytes of OAM, plus
the addressing, data, and counter latches exposed to the CPU. A scanline
call composes visible lines 1 through 224 into a 256×224 BGR555 framebuffer.
The host converts the completed buffer into RGB PPM when requested.

Mode 0, 1, and 3 backgrounds use planar tile rows, decoded with compact
lookup tables. Mode 7 samples a line with signed affine coordinates and
supports EXTBG and direct color. A separate OBJ pass evaluates OAM forward
and applies the tile budget in reverse sprite order. The compositor handles
layer priority, main and sub screens, window rules, fixed or sub-screen
color math, and brightness.

The renderer consumes one PPU state per line. Pixel-phase register changes,
fetch and memory contention, mosaic, Modes 2/4/5/6, and complete
overscan/interlace/hires output require further work.
[PPU and compositor](../src/ppu.inc)

## Audio-side CPU

The optional `IPL` build maps a 64-byte S-SMP initial program into the
SPC700 address space. An integer remainder tracks the nominal ratio between
the master clock and the SPC700 clock. The core synchronizes it at HBlank
and before CPU access to the four communication ports. The current
implementation steps whole SPC700 instructions and advances its prescalers
and timer outputs from instruction cycle counts.

The audio subsystem includes the SPC700, its RAM and IPL overlay, timers,
bidirectional ports, and readable/writeable S-DSP register storage. Phase-exact
shared-RAM arbitration, DSP voice and echo synthesis, and PCM output are
future work. The integration test assembles its own diagnostic IPL.
[APU bus and timing](../src/apu.inc) · [SPC700 interpreter](../src/spc_cpu.inc) · [Diagnostic IPL](../tests/apu_ipl.asm)

## Diagnostics and verification

Every modeled bus event contributes to the total count and rolling hash.
Linux and macOS can also capture the first 65,536 events without changing the
summary. Each 16-byte record is little-endian `<QII`. It contains a
64-bit master tick, a 32-bit tagged guest address, and a 32-bit value field
whose low byte carries the bus data. The address high byte uses tags 0
through 3 for CPU read, CPU write, DMA read, and DMA write.

The original cartridge writes a verdict to WRAM at `$1FF0`. A successful
run reports ASCII `PASS` as `wram=50415353`. The harness checks exact
pixels, DMA and IRQ ordering, monotonic trace ticks, mapper and copier-header
variants, deterministic replay, and error paths. Separate PPU assertions
cover memory ports, brightness, sprite overflow, windows, color math, and
exact Mode 0, 3, and 7 pixel values. The APU variant checks a bidirectional
handshake using the diagnostic IPL.
[Diagnostic cartridge](../tests/diagnostic.asm) · [Integration harness](../tests/test_diagnostic.py) · [PPU checks](../tests/ppu_test.asm)

| Environment | Observed verification |
| --- | --- |
| macOS via Rosetta | Diagnostic, APU handshake, standalone PPU checks |
| Linux binary under QEMU | Diagnostic frame and complete captured trace matched macOS byte for byte. Standalone PPU checks passed. |
| BIOS image under QEMU, 486 CPU and 2 MiB | Base and APU diagnostic summaries matched the x86-64 macOS runner. |
| DOS with FreeDOS and CWSDPMI under QEMU, 486 CPU and 2 MiB | Base diagnostic summary matched the x86-64 macOS runner. |
| Windows x64 | PE build and headers checked. Native execution is pending. |

These checks establish coverage for the fixture and reproducibility across
the tested builds. Physical SNES conformance and performance on physical PCs
remain unverified. [Build and test targets](../Makefile)
