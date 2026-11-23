"""Compare the 386-compatible BIOS image against the native x86-64 runner."""
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main():
    qemu, disk, host, rom, steps, *runner = sys.argv[1:]
    expected = subprocess.check_output(
        [*runner, str(Path(host).resolve()), rom, steps], text=True
    ).strip()
    with tempfile.TemporaryDirectory() as tmp:
        debug = Path(tmp) / "debug.txt"
        args = [
            qemu, "-cpu", "486", "-m", "2M", "-display", "none",
            "-serial", "none", "-monitor", "none", "-nic", "none",
            "-debugcon", f"file:{debug}", "-boot", "a",
            "-drive", f"file={disk},format=raw,if=floppy,readonly=on",
        ]
        with subprocess.Popen(args) as vm:
            try:
                until = time.monotonic() + 10
                observed = ""
                while time.monotonic() < until and vm.poll() is None:
                    if debug.exists():
                        observed = debug.read_text()
                        if "\n" in observed:
                            break
                    time.sleep(0.025)
                assert observed.strip() == expected, (observed, expected)
            finally:
                if vm.poll() is None:
                    vm.terminate()
                    vm.wait(timeout=5)
    print(expected)


if __name__ == "__main__":
    main()
