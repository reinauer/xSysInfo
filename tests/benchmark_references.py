#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Stefan Reinauer
"""Check production reference labels, clock units and row identities."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src/benchmark.h").read_text()
source = (ROOT / "src/benchmark.c").read_text()
struct = re.search(r"typedef struct \{[^}]+\} ReferenceSystem;", header)[0]
constants = "\n".join(line for line in header.splitlines()
                      if line.startswith(("#define NUM_REFERENCE", "#define REF_")))
table = re.search(r"const ReferenceSystem reference_systems\[.*?\n\};",
                  source, re.S)[0]
start = source.index("void format_reference_label(")
end = source.index("\n}\n", start) + 2
formatter = source[start:end]

checks = r'''
static void label(int row, int precise, const char *expected)
{
    char buffer[32];
    format_reference_label(buffer, sizeof(buffer), &reference_systems[row],
                           precise);
    assert(strcmp(buffer, expected) == 0);
}

int main(void)
{
    const int rows[] = {REF_A500, REF_B2000, REF_A1200, REF_A3000,
                        REF_A4000_040, REF_A4000_060};
    const char *names[] = {"A500", "B2000", "A1200", "A3000", "A4000", "A4000"};
    const char *cpus[] = {"68000", "68000", "EC020", "68030", "68040", "68060"};
    ReferenceSystem ntsc = reference_systems[REF_A500];
    char buffer[32] = "unchanged";
    char small[5] = {'x', 'x', 'x', 'x', 'x'};
    int i;

    assert(NUM_REFERENCE_SYSTEMS == 6);
    for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
        unsigned width = 0;
        const char *p;
        assert(rows[i] == i);
        assert(strcmp(reference_systems[i].name, names[i]) == 0);
        assert(strcmp(reference_systems[i].cpu, cpus[i]) == 0);
        format_reference_label(buffer, sizeof(buffer), &reference_systems[i], 0);
        /* Classic TightText allows 121 pixels before the comparison factor. */
        for (p = buffer; *p; p++) width += *p == ' ' ? 4 : 7;
        assert(width < 121);
    }
    assert(reference_systems[REF_A500].mhz_x100 == 709);
    assert(reference_systems[REF_A1200].mhz_x100 == 1419);
    assert(reference_systems[REF_A3000].mhz_x100 == 2500);
    label(REF_A500, 0, "A500  68000 7MHz");
    label(REF_A500, 1, "A500  68000 7.09MHz");
    label(REF_A1200, 0, "A1200 EC020 14MHz");
    label(REF_A1200, 1, "A1200 EC020 14.19MHz");
    label(REF_A3000, 1, "A3000 68030 25MHz");
    label(REF_A4000_060, 0, "A4000 68060 50MHz");
    ntsc.mhz_x100 = 716;
    format_reference_label(buffer, sizeof(buffer), &ntsc, 1);
    assert(strcmp(buffer, "A500  68000 7.16MHz") == 0);
    format_reference_label(small, 4, &ntsc, 1);
    assert(strcmp(small, "A50") == 0 && small[4] == 'x');
    strcpy(buffer, "unchanged");
    format_reference_label(buffer, 0, &ntsc, 1);
    format_reference_label(buffer, sizeof(buffer), NULL, 1);
    format_reference_label(NULL, 10, &ntsc, 1);
    assert(strcmp(buffer, "unchanged") == 0);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="xsysinfo-references-") as tmp:
    tmp = Path(tmp)
    test = tmp / "test.c"
    test.write_text("\n".join([
        "#include <assert.h>\n#include <stdio.h>\n#include <string.h>",
        "typedef unsigned long ULONG;\ntypedef int BOOL;",
        struct, constants, table, formatter, checks,
    ]))
    binary = tmp / "test"
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c99", "-Wall",
                    "-Wextra", "-Werror", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Reference labels, clock units and row identities passed")
