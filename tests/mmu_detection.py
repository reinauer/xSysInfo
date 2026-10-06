#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Stefan Reinauer
"""Run production detection/formatting with mocked Amiga APIs on the host."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def enum(name):
    source = (ROOT / "src/hardware.h").read_text()
    return re.search(r"typedef enum \{[^}]+\} " + name + ";", source)[0]


with tempfile.TemporaryDirectory(prefix="xsysinfo-mmu-detection-") as tmp:
    tmp = Path(tmp)
    constants = "\n".join(line for line in (ROOT / "src/cpu.h").read_text().splitlines()
                          if line.startswith("#define ASM_CPU_"))
    (tmp / "production.h").write_text("\n".join([
        enum("CPUType"), enum("FPUType"), enum("MMUType"),
        enum("MMUTranslationState"), constants,
    ]))
    (tmp / "functions.h").write_text("\n".join([
        function("src/hardware.c", "void detect_mmu(void)"),
        function("src/format.c", "static void format_mmu_value("),
        function("src/which.c", "static void build_mmu_string("),
    ]))
    binary = tmp / "test"
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c99", "-Wall",
                    "-Wextra", "-Werror", "-I", str(tmp),
                    str(ROOT / "tests/mmu_detection.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
