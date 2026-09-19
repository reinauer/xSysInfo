// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

/*
 * RAMSEY latches its control register at the next DRAM refresh. Count
 * these transitions against the independent chipset E-clock to estimate
 * the motherboard clock on A3000/A4000 systems.
 *
 * Reference: Commodore A4000 Service Addendum, RAMSEY specification,
 * pp. 6-32/6-33 (control-register readback and refresh clock counts).
 * https://megaburken.net/~patrik/A3000/390541-0x_Ramsey_specification.pdf
 * The published clock counts are incorrect. Use Chris Hooper's measured
 * counts from ZIPTest (156/240/372); its 240-clock interval was also
 * verified with a logic analyzer:
 * https://github.com/cdhooper/amiga_ziptest/blob/89ba332a4a37d8df5920dbf4b660583f6f2e7be7/ziptest.c#L408
 */

#include <stdint.h>
#include <devices/timer.h>
#include <proto/timer.h>

#include "busclock.h"
#include "hardware.h"
#include "cpu.h"
#include "debug.h"

extern struct Device *TimerBase;
extern struct ExecBase *SysBase;

#define BUS_SHORT_PAIRS 32
#define BUS_LONG_PAIRS 128
#define BUS_SAMPLES 3
#define BUS_POLL_LIMIT 256
#define BUS_POLL_BUDGET 32768

enum {
    BUS_OK,
    BUS_CONTROL_CHANGED,
    BUS_LATCH_TIMEOUT,
    BUS_RESTORE_TIMEOUT,
    BUS_NO_DELAY,
    BUS_BAD_TIMER
};

static struct {
    UBYTE original, alternate;
    UWORD pairs, delayed, budget;
    ULONG ticks, eclock;
    unsigned status;
} probe;

static inline BOOL __attribute__((always_inline))
wait_for_ramsey(UBYTE value, UWORD *budget, UWORD *delayed)
{
    volatile UBYTE *control = (volatile UBYTE *)RAMSEY_CTRL;
    UWORD polls;

    /* Inlining keeps the caller's counters in registers and avoids a
     * function call between reading a transition and writing the next. */
    for (polls = 0; polls < BUS_POLL_LIMIT && *budget; polls++) {
        (*budget)--;
        if (*control == value) {
            if (polls != 0)
                (*delayed)++;
            return TRUE;
        }
    }
    return FALSE;
}

/* Private entry from RunRamseyProbe(): supervisor mode, interrupts masked.
 * No DOS/Exec calls or diagnostics are allowed here. ReadEClock is documented
 * as callable from interrupts. Keep each sample shorter than a CIA rollover.
 */
ULONG sample_ramsey_refreshes(void)
{
    volatile UBYTE *control = (volatile UBYTE *)RAMSEY_CTRL;
    struct EClockVal start, end;
    ULONG end_frequency;
    UWORD pair, budget = BUS_POLL_BUDGET, delayed = 0;
    UBYTE original = probe.original, alternate = probe.alternate;

    probe.status = BUS_CONTROL_CHANGED;
    if (*control != probe.original)
        return FALSE;

    probe.status = BUS_LATCH_TIMEOUT;
    probe.eclock = ReadEClock(&start);
    for (pair = 0; pair < probe.pairs; pair++) {
        *control = alternate;
        if (!wait_for_ramsey(alternate, &budget, &delayed))
            goto restore;
        *control = original;
        if (!wait_for_ramsey(original, &budget, &delayed))
            goto restore;
    }
    end_frequency = ReadEClock(&end);
    probe.ticks = end.ev_lo - start.ev_lo;
    probe.status = BUS_BAD_TIMER;
    if (!probe.ticks || !probe.eclock || end_frequency != probe.eclock ||
        end.ev_hi != start.ev_hi + (end.ev_lo < start.ev_lo) ||
        probe.ticks > probe.eclock / 50)
        goto restore;

    /* The first read can already see a real transition if the boundary
     * fell during the write or loop bookkeeping. Require an observed wait
     * for at least half the transitions, plus the duration checks below. */
    probe.status = delayed >= probe.pairs ?
                   BUS_OK : BUS_NO_DELAY;

restore:
    probe.delayed = delayed;
    probe.budget = budget;
    /* Also overwrite a pending, not yet latched change on failure. */
    *control = original;
    budget = BUS_POLL_LIMIT;
    if (!wait_for_ramsey(original, &budget, &delayed))
        probe.status = BUS_RESTORE_TIMEOUT;
    return probe.status == BUS_OK;
}

void measure_bus_frequency(void)
{
    static const UWORD refresh_clocks[] = {156, 240, 372};
    ULONG pair_clocks, short_ticks, short_eclock, expected_ticks;
    struct EClockVal now;
    ULONG rate, low = ~0UL, high = 0, sum = 0;
    UWORD refresh, alternate_refresh, i;

    hw_info.bus_mhz = 0;
    if (!TimerBase || TimerBase->dd_Library.lib_Version < 36 ||
        SysBase->LibNode.lib_Version < 36 || hw_info.gary_type != FAT_GARY ||
        (hw_info.ramsey_rev != 0x0d && hw_info.ramsey_rev != 0x0f) ||
        hw_info.cpu_type < CPU_68030 || hw_info.cpu_type > CPU_68LC060)
        return;

    short_eclock = ReadEClock(&now);
    if (short_eclock < 700000 || short_eclock > 720000)
        return;

    probe.original = GetRamseyCtrl();
    refresh = (probe.original & RAMSEY_REFRESH_MODE) >> 5;
    if (refresh == 3 || (probe.original & 0x80))
        return; /* Refresh disabled or production-test mode. */

    if (!(probe.original & RAMSEY_BURST_MODE) ||
        hw_info.cpu_type == CPU_68030 || hw_info.cpu_type == CPU_68EC030) {
        /* ZIPTest toggles WRAP to time a fixed refresh rate. A 68030
         * supports the truncated bursts when WRAP is clear. Other CPUs
         * may require complete bursts, so preserve their active WRAP bit. */
        probe.alternate = probe.original ^ RAMSEY_WRAP_MODE;
    } else if (refresh != 0) {
        /* Active bursts: alternate with a shorter refresh interval. Never
         * slow refresh, disable it, or change page mode or memory geometry. */
        probe.alternate = (probe.original & ~RAMSEY_REFRESH_MODE) |
                          ((refresh - 1) << 5);
    } else {
        debug("    bus: burst enabled at shortest refresh interval; skipped\n");
        return;
    }
    alternate_refresh = (probe.alternate & RAMSEY_REFRESH_MODE) >> 5;
    pair_clocks = refresh_clocks[refresh] + refresh_clocks[alternate_refresh];
    debug("    bus: Ramsey $%02lx, control $%02lx/$%02lx, %lu clocks/pair\n",
          (ULONG)hw_info.ramsey_rev, (ULONG)probe.original,
          (ULONG)probe.alternate, pair_clocks);

    for (i = 0; i < BUS_SAMPLES; i++) {
        probe.pairs = BUS_SHORT_PAIRS;
        if (!RunRamseyProbe())
            goto failed;
        short_ticks = probe.ticks;
        short_eclock = probe.eclock;
        probe.pairs = BUS_LONG_PAIRS;
        if (!RunRamseyProbe())
            goto failed;
        debug("    bus: sample %lu: %lu/%lu ticks, EClock %lu Hz\n",
              (ULONG)i + 1, short_ticks, probe.ticks, probe.eclock);
        if (probe.eclock != short_eclock || probe.eclock < 700000 ||
            probe.eclock > 720000 || probe.ticks <= short_ticks)
            return;

        /* The longer run must also scale with its iteration count. This
         * catches a repeatable timing bias that agreement alone would miss. */
        expected_ticks = short_ticks * (BUS_LONG_PAIRS / BUS_SHORT_PAIRS);
        if (probe.ticks + expected_ticks / 10 + 4 < expected_ticks ||
            probe.ticks > expected_ticks + expected_ticks / 10 + 4) {
            debug("    bus: sample duration did not scale with refresh count\n");
            return;
        }

        /* Subtract a shorter run to remove timer-call and initial-phase
         * overhead. Each pair contains one interval at each setting. */
        rate = ((uint64_t)pair_clocks *
                (BUS_LONG_PAIRS - BUS_SHORT_PAIRS) * probe.eclock +
                (uint64_t)(probe.ticks - short_ticks) * 5000) /
               ((uint64_t)(probe.ticks - short_ticks) * 10000);
        if (rate < 1000 || rate > 8000)
            return;
        if (rate < low) low = rate;
        if (rate > high) high = rate;
        sum += rate;
    }

    /* Require agreement within 2%, then use the median. No nominal fallback. */
    if (high - low > low / 50) {
        debug("    bus: inconsistent samples (%lu..%lu MHz/100)\n", low, high);
        return;
    }
    hw_info.bus_mhz = sum - low - high;
    debug("    bus: measured %lu MHz/100\n", hw_info.bus_mhz);
    return;

failed:
    {
        static const char *const reasons[] = {
            "ok", "control changed", "latch timeout", "restore timeout",
            "no refresh delay", "invalid timer"
        };
        debug("    bus: %s (delayed %lu/%lu, budget %lu)\n",
              (ULONG)reasons[probe.status], (ULONG)probe.delayed,
              (ULONG)probe.pairs * 2, (ULONG)probe.budget);
    }
}
