#!/usr/bin/env python3
"""Turn an overlay dump into a corpus in the overlay libraries Pokee loads at run time.

    add_overlay.py [-j N] [--no-build] <build_dir> <overlay.bin> <base_address>
    add_overlay.py --print-dispatch <base_address> <name>

Steps: identify the dump from its header (name, sizes, FNV-1a hash of the header
and the code after it), wrap only that code in a minimal ELF, run psp_recomp
with a per-overlay symbol prefix and the overlay's calls to keep dispatched
(config/overlay_dispatch.txt), then build the overlay libraries (mhfu_overlays) with
N parallel jobs (default 2): the overlay's code, and the library of its slot that
it is linked into. The executable is not relinked. --no-build stops after
psp_recomp and prints the overlay's compile target, so a caller can build many
overlays in one Ninja run. --print-dispatch prints the dispatch= value a corpus
of that overlay should record, for scripts/build_overlays.sh to compare.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

PROFILE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVERLAY_DIR = os.path.join(PROFILE_DIR, "overlays")
DISPATCH_LIST = os.path.join(PROFILE_DIR, "config", "overlay_dispatch.txt")
HEADER_BYTES = 64


def dispatched_calls(base, name):
    """The addresses config/overlay_dispatch.txt lists for this overlay, sorted."""
    addresses = set()
    with open(DISPATCH_LIST, encoding="utf-8") as listing:
        for line in listing:
            fields = line.split("#", 1)[0].split()
            if len(fields) >= 2 and int(fields[0], 0) == base and fields[1] == name:
                addresses.update(int(field, 0) for field in fields[2:])
    return sorted(addresses)


def dispatch_text(addresses):
    """How meta.txt records them: the line CMake and build_overlays.sh compare."""
    return " ".join(f"0x{address:08X}" for address in addresses)


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def parse_header(data, base):
    """Name, image size and code size from the 64-byte overlay header."""
    if data[:4] != b"MWo3":
        raise SystemExit("not an overlay image: expected the MWo3 magic")
    _id, load, code_size, data_size = struct.unpack("<4I", data[4:20])
    if load != base:
        raise SystemExit(f"image loads at {load:#010x}, not at {base:#010x}")
    name = data[32:HEADER_BYTES].split(b"\0")[0].decode("ascii", "replace")
    name = re.sub(r"\W", "_", os.path.splitext(name)[0])
    if not re.fullmatch(r"[A-Za-z0-9_]+", name):
        raise SystemExit("overlay name must normalize to a nonempty ASCII identifier")
    return name, HEADER_BYTES + code_size + data_size, code_size


def main(argv):
    if len(argv) == 4 and argv[1] == "--print-dispatch":
        print(dispatch_text(dispatched_calls(int(argv[2], 0), argv[3])))
        return 0
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0],
                                     usage=__doc__.strip().splitlines()[2].strip())
    parser.add_argument("-j", "--jobs", type=int, default=2, help="parallel build jobs (default 2)")
    parser.add_argument("--no-build", action="store_true", help="recompile only; print the target")
    parser.add_argument("build_dir")
    parser.add_argument("dump_path", type=argparse.FileType("rb"))
    parser.add_argument("base_text")
    options = parser.parse_args(argv[1:])
    build_dir = os.path.abspath(options.build_dir)
    dump_path = os.path.abspath(options.dump_path.name)
    base = int(options.base_text, 0)
    if base < 0 or base > 0xFFFFFFFF or base % 4:
        parser.error("base address must be an aligned 32-bit unsigned address")
    if options.jobs < 1:
        parser.error("jobs must be positive")
    recompiler = os.path.join(build_dir, "psp_recomp.exe" if os.name == "nt" else "psp_recomp")
    with options.dump_path as handle:
        data = handle.read()
    name, image_size, code_size = parse_header(data, base)
    # Only the header and the code identify the image: the game writes into the
    # data section of a loaded overlay.
    digest = fnv1a64(data[:HEADER_BYTES + code_size])
    prefix = f"ovl{base:08X}_{name}_{digest:016X}"
    overlay_root = os.path.realpath(OVERLAY_DIR)
    target = os.path.realpath(os.path.join(overlay_root, prefix))
    if not target.startswith(overlay_root + os.sep):
        parser.error("overlay directory escapes the corpus root")
    os.makedirs(target, exist_ok=True)

    elf_path = os.path.realpath(os.path.join(target, "overlay.elf"))
    metadata_path = os.path.realpath(os.path.join(target, "meta.txt"))
    if not elf_path.startswith(target + os.sep) or not metadata_path.startswith(target + os.sep):
        parser.error("overlay output file escapes its corpus directory")
    # The 64-byte header and the data section are not code. Wrapping the whole
    # image makes the recompiler emit an entry for every word of them, and a
    # call that lands there runs data. The game enters at the first code byte.
    code_base = base + HEADER_BYTES
    code_path = os.path.realpath(os.path.join(target, "code.bin"))
    if not code_path.startswith(target + os.sep):
        parser.error("overlay code file escapes its corpus directory")
    with open(code_path, "wb") as out:
        out.write(data[HEADER_BYTES:HEADER_BYTES + code_size])
    code_base_text = f"0x{code_base:08X}"
    for stale in os.listdir(target):
        if stale.startswith("generated_unit_") or stale.endswith("_registry.cpp") or stale.endswith("_units.hpp"):
            os.remove(os.path.join(target, stale))
    subprocess.run([sys.executable, os.path.join(PROFILE_DIR, "tools", "wrap_overlay.py"),
                    code_path, code_base_text, elf_path], check=True)
    os.remove(code_path)
    dispatch = dispatched_calls(base, name)
    dispatch_options = [option for address in dispatch for option in ("--dispatch", f"0x{address:08X}")]
    # The host's site hooks for this overlay, by its name (config/site_hooks.txt).
    site_hooks = os.path.join(PROFILE_DIR, "config", "site_hooks.txt")
    subprocess.run([recompiler, elf_path, "--auto", target,
                    code_base_text, "--prefix", prefix, *dispatch_options,
                    "--site-hooks", site_hooks, "--site-image", name], cwd=build_dir, check=True)
    # The metadata the host identifies the corpus by. CMake reads it to configure
    # the library entry point. The build does not need an explicit reconfigure:
    # the CONFIGURE_DEPENDS glob over overlays/*/meta.txt notices the new
    # directory and reruns CMake on its own.
    with open(metadata_path, "w") as out:
        out.write(f"base=0x{base:08X}\nname={name}\nsize={image_size}\ncode_size={code_size}\n"
                  f"hash=0x{digest:016X}\nsource={os.path.basename(dump_path)}\n"
                  f"dispatch={dispatch_text(dispatch)}\n")

    if options.no_build:
        print(f"target: overlay_{prefix}")
        return 0
    # A new overlays/*/meta.txt is invisible to a Makefile build until CMake
    # runs again. The CONFIGURE_DEPENDS glob only helps once the build system
    # already knows about that directory.
    source = os.path.dirname(os.path.dirname(PROFILE_DIR))
    subprocess.run(["cmake", "-S", source, "-B", build_dir], check=True)
    subprocess.run(["cmake", "--build", build_dir, "--target", "mhfu_overlays",
                    "-j", str(options.jobs)], check=True)
    print(f"overlay {name}: {code_size} bytes of code at {base:#010x}")
    print(f"library: {os.path.join(build_dir, 'bin', 'overlays')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
