/* SPDX-License-Identifier: BSD-2-Clause */
/* Hunk objects do not preserve the compiler's section alignment. Keep
 * Dhrystone in a separate code hunk and align both input sections there.
 * LoadSeg still chooses the hunk's absolute address at runtime. */
SECTIONS
{
    .dhrystone ALIGN(16) : SUBALIGN(16)
    {
        *dhry_1.o(.text)
        *dhry_2.o(.text)
    }
}
INSERT AFTER .text;
