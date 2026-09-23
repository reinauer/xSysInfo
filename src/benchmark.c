// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Benchmarking (Dhrystone, MIPS, MFLOPS, Chip speed)
 */

#include <string.h>
#include <limits.h>

#include <exec/execbase.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <hardware/cia.h>

#include <proto/exec.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>

#include "xsysinfo.h"
#include "benchmark.h"
#include "hardware.h"
#include "debug.h"
#include "cpu.h"
#include "dhry.h"
#include "locale_str.h"

extern struct ExecBase *SysBase;

/* Global benchmark results */
BenchmarkResults bench_results;

/* Reference system data (placeholder values - to be calibrated); scaled by 100 */
    /* name,  cpu,     mhz,   dhry, mips, mfls */
const ReferenceSystem reference_systems[NUM_REFERENCE_SYSTEMS] = {
    /* A500:  68000 @ 7.09 MHz, no FPU */
    {"A600",  "68000",   7,   1001,   56,    0},
    /* B2000: 68000 @ 7.09 MHz, no FPU, FastRam*/
    {"B2000", "68000",   7,   1408,   81,    0},
    /* A1200: 68EC020 @ 14 MHz, no FPU */
    {"A1200", "EC020",  14,   2550,  145,    0},
    /* A3000: 68030 / 68882 @ 25 MHz */
    {"A3000", "68030",  25,   8300,  475,  285},
    /* A4000: 68040 @ 25 MHz, internal FPU */
    {"A4000", "68040",  25,  32809, 1867,  504},
    /* A4000: 68040 @ 25 MHz, internal FPU */
    {"A4000", "68060",  50,  91000, 5200,  685},
};

/*
 * Keep slow memory tests quick, but force fast regions to run long enough
 * that timer granularity and loop-overhead subtraction do not dominate.
 */
#define MEM_SPEED_FAST_THRESHOLD 50000000UL
#define MEM_SPEED_TARGET_US 200000UL
#define MEM_SPEED_MAX_ITERATIONS 32768UL

void format_reference_label(char *buffer, size_t buffer_size, const ReferenceSystem *ref)
{
    if (!buffer || buffer_size == 0 || !ref) return;

    snprintf(buffer, buffer_size, "%-5s %-5s %luMHz",
             ref->name, ref->cpu, (unsigned long)ref->mhz);
}

/* Timer resources */
static struct MsgPort *timer_port = NULL;
static struct MsgPort *etimer_port = NULL;
static struct timerequest *timer_req = NULL;
struct Device *TimerBase = NULL;
static BOOL timer_open = FALSE;
static struct timerequest *etimer_req = NULL;
struct Device *ETimerBase = NULL;
static BOOL etimer_open = FALSE;


/* External references */
extern HardwareInfo hw_info;

/*
 * Initialize timer for benchmarking
 */
BOOL init_timer(void)
{
    cleanup_timer();

    timer_port = CreatePort(NULL, 0);
    if (!timer_port) return FALSE;

    timer_req = (struct timerequest *)
        CreateExtIO(timer_port, sizeof(struct timerequest));
    if (!timer_req) {
        debug("    init_timer: no timer_req\n");
        cleanup_timer();
        return FALSE;
    }
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
                   (struct IORequest *)timer_req, 0) != 0) {
        debug("    init_timer: no OpenDevice timer_req\n");
        cleanup_timer();
        return FALSE;
    }

    timer_open = TRUE;
    TimerBase = (struct Device *)timer_req->tr_node.io_Device;

    if (SysBase->LibNode.lib_Version < 36) {
        return TRUE;
    }

    etimer_port = CreatePort(NULL, 0);
    if (!etimer_port) {
        debug("    init_timer: no etimer_port, falling back to microhz timer\n");
        return TRUE;
    }

    etimer_req = (struct timerequest *)
        CreateExtIO(etimer_port, sizeof(struct timerequest));
    if (!etimer_req) {
        debug("    init_timer: no etimer_req, falling back to microhz timer\n");
        DeletePort(etimer_port);
        etimer_port = NULL;
        return TRUE;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)etimer_req, 0) != 0) {
        debug("    init_timer: no OpenDevice etimer_req, falling back to microhz timer\n");
        DeleteExtIO((struct IORequest *)etimer_req);
        etimer_req = NULL;
        DeletePort(etimer_port);
        etimer_port = NULL;
        return TRUE;
    }

    ETimerBase = (struct Device *)etimer_req->tr_node.io_Device;
    etimer_open = TRUE;

    return TRUE;
}

/*
 * Cleanup timer
 */
void cleanup_timer(void)
{
    if (timer_open) {
        CloseDevice((struct IORequest *)timer_req);
        timer_open = FALSE;
    }

    if (timer_req) {
        DeleteExtIO((struct IORequest *)timer_req);
        timer_req = NULL;
    }

    if (etimer_open) {
        CloseDevice((struct IORequest *)etimer_req);
        etimer_open = FALSE;
    }

    if (etimer_req) {
        DeleteExtIO((struct IORequest *)etimer_req);
        etimer_req = NULL;
    }

    if (timer_port) {
        DeletePort(timer_port);
        timer_port = NULL;
    }

    if (etimer_port) {
        DeletePort(etimer_port);
        etimer_port = NULL;
    }


    TimerBase = NULL;
    ETimerBase = NULL;
}

BOOL benchmark_timer_available(void)
{
    return TimerBase != NULL;
}

/*
 * CIAA time-of-day counter, used as a Kickstart 1.3 timing fallback.
 * CIAA TOD is a free-running 24-bit counter the OS clocks at the 50/60 Hz
 * vblank tick; reading it needs neither ReadEClock nor GetSysTime, both of
 * which are V36+.
 */
#define CIAA_BASE ((volatile struct CIA *)0xBFE001)

static ULONG read_ciaa_tod(void)
{
    ULONG hi, mid, lo;

    /* 8520 latch protocol: reading the high byte freezes all three TOD
     * registers, reading the low byte releases them, so the 24-bit value
     * is coherent even if it advances mid-read. */
    hi  = CIAA_BASE->ciatodhi;
    mid = CIAA_BASE->ciatodmid;
    lo  = CIAA_BASE->ciatodlow;
    return (hi << 16) | (mid << 8) | lo;
}

ULONG read_benchmark_clock(struct EClockVal *val)
{
    if (!val || !TimerBase) {
        return 0;
    }

    if (ETimerBase && etimer_open) {
        return ReadEClock(val);
    }

    if (SysBase->LibNode.lib_Version >= 36) {
        /*
         * GetSysTime() is a V36+ timer.device call. timeval and EClockVal
         * share the same two ULONG fields, so the diff helper below can
         * reuse them; returning 0 selects its microsecond path.
         */
        GetSysTime((struct timeval *)val);
        return 0;
    }

    /*
     * Kickstart 1.3 has neither ReadEClock nor GetSysTime. Fall back to
     * the CIAA time-of-day counter. It only advances at the 50/60 Hz
     * vblank rate, so resolution is coarse, but the benchmark loops
     * escalate their iteration counts until an interval spans several
     * ticks. Returning the tick frequency lets EClock_Diff_in_ms scale to
     * microseconds; if the counter never advances the diff is zero and the
     * callers fall back to their CPU-type estimates.
     */
    val->ev_hi = 0;
    val->ev_lo = read_ciaa_tod();
    return hw_info.is_pal ? 50 : 60;
}

/* Use short interrupt-disabled windows for clock estimates on V36+.
 * Forbid stays active across the samples, while Enable lets pending device
 * interrupts run between them. The Kickstart 1.3 TOD clock is too coarse
 * for these windows, so it keeps the existing interrupt-enabled timing.
 */
static BOOL frequency_eclock_available(void)
{
    return TimerBase && TimerBase->dd_Library.lib_Version >= 36 &&
           SysBase->LibNode.lib_Version >= 36;
}

/* Keep the sampling support small; the timed loops are explicit assembly. */
typedef struct {
    const char *reason;
    ULONG expected_rate, first_rate, last_rate;
    struct EClockVal start, end;
} FrequencyFailure;

static ULONG __attribute__((optimize("Os")))
frequency_sample(ULONG loops, BOOL fpu, ULONG *frequency,
                 FrequencyFailure *failure)
{
    struct EClockVal start, end;
    ULONG first_rate, last_rate;
    const char *reason = NULL;

    Disable();
    first_rate = ReadEClock(&start);
    if (fpu) {
        __asm__ volatile(
            "fmove.w #1,fp1\n\t"
            "1: fdiv.x fp1,fp1\n\t"
            "subq.l #1,%0\n\t"
            "bne.s 1b\n\t"
            /* Wait for the external FPU to finish the final division. */
            "fmove.l fp1,d1"
            : "+d"(loops)
            :
            : "cc", "d1", "fp1");
    } else {
        __asm__ volatile(
            "1: subq.l #1,%0\n\t"
            "bne.s 1b"
            : "+d"(loops)
            :
            : "cc");
    }
    last_rate = ReadEClock(&end);
    Enable();

    if (!first_rate)
        reason = "zero EClock frequency";
    else if (first_rate != last_rate ||
             (*frequency && first_rate != *frequency))
        reason = "EClock frequency changed";
    else if (end.ev_hi != start.ev_hi + (end.ev_lo < start.ev_lo))
        reason = "invalid EClock interval";
    else if (end.ev_lo == start.ev_lo)
        reason = "EClock did not advance";
    if (reason) {
        /* Preserve the first failed sample; print only after Permit(). */
        if (!failure->reason) {
            failure->reason = reason;
            failure->expected_rate = *frequency;
            failure->first_rate = first_rate;
            failure->last_rate = last_rate;
            failure->start = start;
            failure->end = end;
        }
        return 0;
    }
    *frequency = first_rate;
    return end.ev_lo - start.ev_lo;
}

/* Return the equivalent runtime in microseconds for reference_loops, so
 * the CPU/FPU calibration factors use the same units as the legacy path.
 * Estimate the slope between two established loop runs, so fixed timer,
 * setup and cache costs cancel even if a tiny loop behaves differently.
 * Keep the baseline well below the long run to leave useful loop work
 * after subtraction. Also time a tiny loop to quantify the old bias.
 * Sum E-clock ticks before conversion to avoid rounding each sample.
 */
static ULONG __attribute__((optimize("Os")))
frequency_loop_time_once(ULONG reference_loops, BOOL fpu, ULONG attempt)
{
    const ULONG samples = 64;
    const ULONG short_loops = fpu ? 2 : 16;
    const ULONG max_loops = 2097152;
    ULONG loops = short_loops * 2;
    ULONG frequency = 0, ticks = 0, shorter = 0, total = 0, result = 0, i = 0;
    ULONG short_total = 0, long_total = 0, low = ~0UL, high = 0;
    ULONG baseline_loops = 0, baseline = 0, baseline_total = 0;
    ULONG uncorrected = 0;
    LONG bias_us = 0;
    ULONG loop_total = 0, sample_loops = 0;
    ULONG min_delta = 0, max_ticks = 0, next_loops;
    FrequencyFailure failure = {0};
    const char *reason = NULL;
    BOOL measuring = FALSE;

    Forbid();
    /* Require at least 1 ms of loop work after subtracting the baseline
     * sample, even at the smallest count used below. Timer/setup overhead
     * must not make calibration accept a nearly empty interval. Longer
     * samples reduce E-clock quantization and residual setup/cache bias.
     * Keep measured intervals within 2 ms; a complete Disable/Enable
     * window also includes timer work outside those timestamps. */
    for (;;) {
        shorter = frequency_sample(short_loops, fpu, &frequency, &failure);
        if (!shorter)
            goto done;
        min_delta = (frequency - 1) / 1000 + 1;
        max_ticks = frequency / 500;
        if (shorter >= max_ticks) {
            reason = "timer overhead leaves no loop interval";
            goto done;
        }
        ticks = frequency_sample(loops, fpu, &frequency, &failure);
        if (!ticks)
            goto done;
        if (ticks > max_ticks) {
            reason = "calibration sample exceeds 2 ms";
            goto done;
        }
        sample_loops = loops - 3 * (loops / 16);
        baseline_loops = loops / 16;
        if (baseline_loops < short_loops)
            baseline_loops = short_loops;
        if (ticks > shorter &&
            (uint64_t)(ticks - shorter) * (sample_loops - baseline_loops) >=
            (uint64_t)min_delta * (loops - short_loops))
            break;
        if (loops >= max_loops) {
            reason = "calibration loop limit";
            goto done;
        }
        next_loops = loops * 2;
        if (next_loops > max_loops)
            next_loops = max_loops;
        if (ticks > shorter) {
            /* Reserve 100 us for timer variation when limiting growth.
             * Slower CPUs can use a count between successive doublings. */
            ULONG budget = max_ticks - ((frequency - 1) / 10000 + 1);
            if (shorter >= budget) {
                reason = "insufficient loop interval within 2 ms";
                goto done;
            }
            uint64_t limit = short_loops +
                (uint64_t)(budget - shorter) * (loops - short_loops) /
                (ticks - shorter);
            if (next_loops > limit)
                next_loops = limit;
        }
        if (next_loops <= loops) {
            reason = "insufficient loop interval within 2 ms";
            goto done;
        }
        loops = next_loops;
    }
    measuring = TRUE;
    for (i = 0; i < samples; i++) {
        /* Vary the long count downward to avoid repeatedly sampling at
         * the same E-clock phase. Each adjacent AB/BA pair uses the same
         * count, and no sample exceeds the calibrated length. */
        sample_loops = loops - ((i / 2) & 3) * (loops / 16);
        shorter = frequency_sample(short_loops, fpu, &frequency, &failure);
        /* Balance sample order so the long loop does not always benefit
         * from the preceding baseline warming the timer/cache paths. */
        if (i & 1) {
            ticks = frequency_sample(sample_loops, fpu, &frequency, &failure);
            baseline = frequency_sample(baseline_loops, fpu, &frequency, &failure);
        } else {
            baseline = frequency_sample(baseline_loops, fpu, &frequency, &failure);
            ticks = frequency_sample(sample_loops, fpu, &frequency, &failure);
        }
        if (failure.reason)
            goto done;
        if (baseline <= shorter) {
            reason = "baseline sample not longer than tiny sample";
            goto done;
        }
        if (ticks <= baseline) {
            reason = "long sample not longer than baseline";
            goto done;
        }
        if (ticks > max_ticks) {
            reason = "sample exceeds 2 ms";
            goto done;
        }
        short_total += shorter;
        baseline_total += baseline;
        long_total += ticks;
        if (ticks - baseline < low) low = ticks - baseline;
        if (ticks - baseline > high) high = ticks - baseline;
        total += ticks - baseline;
        loop_total += sample_loops - baseline_loops;
    }
    if (total < samples * min_delta) {
        reason = "average loop interval below 1 ms";
        goto done;
    }
    /* Convert the accumulated interval before scaling by reference_loops
     * to avoid overflow at large counts. This loses less than 1 us over
     * the entire batch, whose useful work is at least 64 ms. */
    result = ((uint64_t)total * 1000000 / frequency) * reference_loops /
             loop_total;
    if (!result)
        reason = "loop interval rounds to zero";
done:
    Permit();
    if (g_debug_enabled && result) {
        /* Compare against the tiny baseline without assuming a nominal MHz.
         * The intercept estimates the fixed cost missed by that baseline.
         * Divide before converting to microseconds to keep products in range. */
        LONG bias_ticks = (LONG)(baseline_total - short_total) -
            (LONG)((uint64_t)total * samples * (baseline_loops - short_loops) /
                   loop_total);
        uncorrected = ((uint64_t)(long_total - short_total) * 1000000 /
                       frequency) * reference_loops /
                      (loop_total + samples * (baseline_loops - short_loops));
        bias_us = (uint64_t)(bias_ticks < 0 ? -bias_ticks : bias_ticks) *
                  1000000 / ((uint64_t)frequency * samples);
        if (bias_ticks < 0)
            bias_us = -bias_us;
    }
    debug("    clock %s: %lu short, %lu..%lu long, %lu triples, EClock %lu Hz\n",
          (ULONG)(fpu ? "FPU" : "CPU"), short_loops,
          loops - 3 * (loops / 16), loops, i, frequency);
    debug("      ticks short/long: %lu/%lu, delta range %lu..%lu\n",
          short_total, long_total, i ? low : 0, high);
    debug("      baseline: %lu loops, %lu ticks total\n",
          baseline_loops, baseline_total);
    if (result)
        debug("      normalized us tiny/slope: %lu/%lu; fixed bias %ld us\n",
              uncorrected, result, bias_us);
    debug("      minimum loop delta %lu ticks, sample limit %lu ticks\n",
          min_delta, max_ticks);
    if (!result) {
        debug("      attempt %lu/3 rejected at %s %lu: %s\n", attempt,
              (ULONG)(!measuring ? "calibration" :
                      i < samples ? "triple" : "conversion"),
              measuring && i < samples ? i + 1 : 0,
              (ULONG)(failure.reason ? failure.reason : reason));
        debug("      loops short/long: %lu/%lu, ticks short/long: %lu/%lu\n",
              short_loops, measuring ? sample_loops : loops, shorter, ticks);
        if (measuring)
            debug("      baseline loops/ticks: %lu/%lu\n", baseline_loops, baseline);
        if (failure.reason)
            debug("      EClock Hz expected/start/end: %lu/%lu/%lu; "
                  "ticks %08lx:%08lx -> %08lx:%08lx\n",
                  failure.expected_rate, failure.first_rate, failure.last_rate,
                  failure.start.ev_hi, failure.start.ev_lo,
                  failure.end.ev_hi, failure.end.ev_lo);
    }
    return result;
}

static ULONG frequency_loop_time(ULONG reference_loops, BOOL fpu)
{
    ULONG attempt, result;

    /* Retry whole batches, including calibration, to avoid selecting only
     * favourable pairs. Each attempt restores scheduling and interrupts. */
    for (attempt = 1; attempt <= 3; attempt++) {
        result = frequency_loop_time_once(reference_loops, fpu, attempt);
        if (result)
            return result;
    }
    return 0;
}

/*
 * Returns the CPU-frequencies in MHz scaled by 100
*/
ULONG get_mhz_cpu(void)
{

    ULONG multiplier, loop, maxMultiplier, startMultiplier;
    uint64_t count = 0, tmp, mhz = 0;
    APTR test; //for testing the memtype we are running in

    // correction factors for fast CPUs!
    switch (hw_info.cpu_type)
        {
        case CPU_68040:
        case CPU_68EC040:
        case CPU_68LC040:
        case CPU_68060:
        case CPU_68EC060:
        case CPU_68LC060:
        case CPU_68080:
        case CPU_EMU:
            maxMultiplier = MAX_MULTIPLY*16;
            startMultiplier = MAX_MULTIPLY/16;
            break;
        default:
            maxMultiplier = MAX_MULTIPLY;
            startMultiplier = 1;
            break;
        }


    for (multiplier = startMultiplier; multiplier <= maxMultiplier && count < MIN_MHZ_MEASURE; multiplier *= 2)
    {
        loop = CPULOOPS * multiplier;
        count = frequency_eclock_available() ?
                frequency_loop_time(loop, FALSE) : measure_loop_overhead(loop);
        /* A failed measurement must not masquerade as a nominal clock. */
        if (!count && frequency_eclock_available())
            return 0;
        if (multiplier >= maxMultiplier || count >= MIN_MHZ_MEASURE) {
            break;
        }
    }

    tmp = BASE_FACTOR * (uint64_t)multiplier;

    if (count > 0)
    {
        // avoid div/0

        // empirical correction factors
        switch (hw_info.cpu_type)
        {
        case CPU_68000:
        case CPU_68010:
            // check whether we run in fastram or chipram (huge difference in speed calc!)
            test = __builtin_return_address(0); // this gets the return address, which tells me if we are running in fast ram
            // check whether it is fast mem!
            if ((long unsigned int)test >= 0x200000 && (long unsigned int)test < 0xC00000)
            {
                // real fastmem!
                tmp *= 204;
            }
            else
            { // chip or ranger mem
                tmp *= 282;
            }
            break;
        case CPU_68020:
        case CPU_68EC020:
            tmp *= 88;
            break;
        case CPU_68030:
        case CPU_68EC030:
            /* MC68030 manual, sections 11.3.4, 11.6.9 and 11.6.15:
             * cached SUBQ.L (2 clocks) + taken Bcc (6 clocks).
             * Differencing cancels the final, non-taken branch. */
            if (frequency_eclock_available() && hw_info.icache_enabled)
                tmp = (uint64_t)loop * 8 * 100;
            else
                tmp *= 88;
            break;
        case CPU_68040:
        case CPU_68EC040:
        case CPU_68LC040:
            tmp *= 3253;
            count *=100;
            break;
        case CPU_68060:
        case CPU_68EC060:
        case CPU_68LC060:
        case CPU_68080:
            tmp *= 1085;
            if (hw_info.mmu_enabled || hw_info.cpu_type == CPU_68080) {
                if (hw_info.super_scalar_enabled) {
                    count *=100;
                }
                else {
                    count *=50; // without super scalar the cpu seems 2 times slower!
                }
            }
            else {
                count *=20; // without mmu the 68060 seems 5 times slower!
            }
            break;
        default:
            tmp *= 100;
            break;
        }
        mhz = tmp / count;
        debug("    cpu_mhz: results: %lu %lu %lu %lu\n", (ULONG)count, (ULONG)tmp, (ULONG)mhz, multiplier);
    }
    else
    {

        /* Fallback: estimate based on CPU type and system */
        switch (hw_info.cpu_type)
        {
        case CPU_68000:
        case CPU_68010:
            mhz = 709; /* Standard 68000 */
            break;
        case CPU_68020:
        case CPU_68EC020:
            mhz = 1418; /* Common for A1200/accelerators */
            break;
        case CPU_68030:
        case CPU_68EC030:
            mhz = 2500; /* Common for 030 accelerators */
            break;
        case CPU_68040:
        case CPU_68LC040:
            mhz = 2500; /* A4000 stock */
            break;
        case CPU_68060:
        case CPU_68EC060:
        case CPU_68LC060:
            mhz = 5000; /* Common 060 speed */
            break;
        case CPU_68080:
            mhz = 8000; /* Common 080 speed */
            break;
        default:
            mhz = 709;
            break;
        }
    }

    return (ULONG) mhz;
}

/*
 * Returns the FPU frequency in MHz scaled by 100
*/
ULONG get_mhz_fpu(void)
{

    /* make some sanity tests:
     *  No FPU -> nothing,
     *  Unknown FPU -> nothing
     *  68EC/LC040/060 -> No FPU nothing
     *  68040 or 68060 CPU: Same as CPU Frequency
     */

    if (FPU_NONE == hw_info.fpu_type || FPU_UNKNOWN == hw_info.fpu_type)
    {
        return 0;
    }

    if (!benchmark_timer_available()) return 0;

    switch (hw_info.cpu_type)
    {
    case CPU_68LC040:
    case CPU_68EC040:
    case CPU_68EC060:
    case CPU_68LC060:
        return 0;
    case CPU_68040:
    case CPU_68060:
    case CPU_68080:
        /* The integrated FPU shares the CPU result, including failure. */
        return hw_info.cpu_mhz;
    default:
        break;
    }

    ULONG loop, multiplier, overhead;
    ULONG E_Freq;
    struct EClockVal start, end;
    uint64_t count = 0, tmp, mhz = 0;
    for (multiplier = 1; multiplier <= MAX_MULTIPLY && count < MIN_MHZ_MEASURE; multiplier *= 2)
    {
        loop = FPULOOPS * multiplier;
        if (frequency_eclock_available()) {
            count = frequency_loop_time(loop, TRUE);
            overhead = frequency_loop_time(loop, FALSE);
            if (!overhead || count <= overhead)
                return 0;
        } else {
            Forbid();
            E_Freq = read_benchmark_clock(&start);
            __asm__ volatile(
                "fmove.w #1,fp1\n\t"
                "1:\t\tfdiv.x fp1,fp1\n\t"
                "subq.l\t#1,%0\n\t"
                "bne.s\t1b\n\t"
                : "+d"(loop)
                :
                : "cc", "fp1");

            E_Freq = read_benchmark_clock(&end);
            Permit();
            loop = FPULOOPS * multiplier;
            count = EClock_Diff_in_ms(&start, &end, E_Freq);
            overhead = measure_loop_overhead(loop);
        }
        if (count > overhead) {
            count -= (uint64_t) overhead;
        }
        if (multiplier >= MAX_MULTIPLY || count >= MIN_MHZ_MEASURE) {
            break;
        }
    }

    tmp = BASE_FACTOR * (uint64_t) multiplier;
    debug("    fpu_mhz: results: %lu %lu %lu\n", (ULONG)count, (ULONG)tmp, overhead);

    if (count > 0)
    {
        // avoid div/0

        // empirical correction factors
        switch (hw_info.fpu_type)
        {
        case FPU_68881:
            tmp *= 79;
            break;
        case FPU_68882:
            tmp *= 79;
            break;
        default:
            break;
        }
        mhz = tmp / count;
    }
    else
    {
        /* Fallback: estimate based on FPU type and system */

        switch (hw_info.fpu_type)
        {
        case FPU_NONE:
            mhz = 0;
            break;
        case FPU_68881:
            mhz = 1400;
            break;
        case FPU_68882:
            mhz = 2500;
            break;
        case FPU_68040:
            mhz = 5000;
            break;
        case FPU_68060:
            mhz = 5000;
            break;
        case FPU_68080:
            mhz = 8000;
            break;
        case FPU_UNKNOWN:
        default:
            mhz = 0;
            break;
        }
    }

    return (ULONG)mhz;
}
/*
 * Get current timer ticks (microseconds)
 */
uint64_t get_timer_ticks(void)
{
    struct timeval tv;

    if (!TimerBase) return 0;
    if (SysBase->LibNode.lib_Version < 36) return 0;  /* GetSysTime is V36+ */

    GetSysTime(&tv);

    /* Return microseconds (may wrap around, but OK for short measurements) */
    return (uint64_t)tv.tv_secs * 1000000ULL + tv.tv_micro;
}

/*
 * Get current timer
 */
void get_timer(struct timeval *tv)
{
    if (!TimerBase) return;
    if (SysBase->LibNode.lib_Version < 36) return;  /* GetSysTime is V36+ */

    GetSysTime(tv);
}

/*
 * Wait for specified number of microseconds
 */
void wait_ticks(ULONG ticks)
{
    if (!timer_req || !timer_open) return;

    timer_req->tr_node.io_Command = TR_ADDREQUEST;
    timer_req->tr_time.tv_secs = ticks / 1000000UL;
    timer_req->tr_time.tv_micro = ticks % 1000000UL;

    DoIO((struct IORequest *)timer_req);
}

/* Report loaded placement outside the timed benchmark. */
static void debug_dhrystone_location(const char *name, APTR address)
{
    debug("  bench: %s at $%08lx, offset %lu/16, memory flags $%08lx\n",
          (ULONG)name, (ULONG)address, (ULONG)address & 15UL,
          TypeOfMem(address));
}

/* Forbid() is held by the caller. Calibration keeps interrupts enabled so
 * an initially unknown iteration time cannot lose E-clock overflows. */
static ULONG dhrystone_sample(ULONG loops, BOOL disable_interrupts,
                             ULONG *frequency, const char **failure)
{
    struct EClockVal start, end;
    ULONG first_rate, last_rate;

    if (disable_interrupts) Disable();
    first_rate = ReadEClock(&start);
    Dhry_Run(loops);
    last_rate = ReadEClock(&end);
    if (disable_interrupts) Enable();

    if (!first_rate)
        *failure = "zero EClock frequency";
    else if (first_rate != last_rate ||
             (*frequency && first_rate != *frequency))
        *failure = "EClock frequency changed";
    else if (end.ev_hi != start.ev_hi + (end.ev_lo < start.ev_lo))
        *failure = "invalid EClock interval";
    else if (end.ev_lo == start.ev_lo)
        *failure = "EClock did not advance";
    if (*failure)
        return 0;

    *frequency = first_rate;
    return end.ev_lo - start.ev_lo;
}

static ULONG run_dhrystone_chunks(void)
{
    const ULONG max_loops = 5000000UL;
    const ULONG max_chunks = 128;
    ULONG loops = 1000, frequency = 0, ticks = 0;
    ULONG calibration_loops = 0, calibration_ticks = 0;
    ULONG target_ticks = 0, limit_ticks = 0, chunks = 0, total_loops = 0;
    ULONG shortest = ~0UL, longest = 0, attempt;
    uint64_t total_ticks = 0, next_loops, goal_ticks;
    const char *failure = NULL;

    if (!Dhry_Initialize()) return 0;

    Forbid();
    for (attempt = 0; attempt < 3; attempt++) {
        ticks = dhrystone_sample(loops, FALSE, &frequency, &failure);
        if (!ticks) goto done;
        /* Aim for at least 10 ms of calibration on fast processors. */
        if (ticks >= frequency / 100 || loops == max_loops || attempt == 2)
            break;
        next_loops = (uint64_t)loops * frequency / (100ULL * ticks) + 1;
        loops = next_loops > max_loops ? max_loops : (ULONG)next_loops;
    }
    calibration_loops = loops;
    calibration_ticks = ticks;

    /* Target 75 ms, with a 90 ms rejection limit. Also bound both by the
     * 16-bit hardware period if a machine reports an unusual E-clock rate.
     * Enable() between chunks lets timer.device account for overflows. */
    limit_ticks = (uint64_t)frequency * 90 / 1000;
    if (limit_ticks > 0xff00) limit_ticks = 0xff00;
    target_ticks = (uint64_t)frequency * 75 / 1000;
    if (target_ticks >= limit_ticks) target_ticks = limit_ticks * 3 / 4;
    if (!target_ticks || (uint64_t)ticks >= (uint64_t)loops * limit_ticks) {
        failure = "iteration too long for EClock chunk";
        goto done;
    }
    next_loops = (uint64_t)loops * target_ticks / ticks;
    loops = next_loops > max_loops ? max_loops : (ULONG)next_loops;
    if (!loops) loops = 1;
    goal_ticks = (uint64_t)frequency * 2;

    /* Keep the initialized records between chunks. Only the timed work
     * contributes to the score; pending interrupts run outside it while
     * task switching remains forbidden. */
    while (total_ticks < goal_ticks && total_loops < max_loops &&
           chunks < max_chunks) {
        if (loops > max_loops - total_loops)
            loops = max_loops - total_loops;
        ticks = dhrystone_sample(loops, TRUE, &frequency, &failure);
        if (!ticks) goto done;
        if (ticks >= limit_ticks) {
            failure = "chunk exceeds EClock timing limit";
            goto done;
        }
        total_loops += loops;
        total_ticks += ticks;
        chunks++;
        if (ticks < shortest) shortest = ticks;
        if (ticks > longest) longest = ticks;

        /* Calibration can include interrupt work. Converge on the target
         * using measured chunks, limiting growth to twice the loop count. */
        next_loops = (uint64_t)loops * target_ticks / ticks;
        if (next_loops > (uint64_t)loops * 2) next_loops = (uint64_t)loops * 2;
        if (next_loops > max_loops) next_loops = max_loops;
        loops = next_loops ? (ULONG)next_loops : 1;
    }
    if (chunks == max_chunks && total_ticks < goal_ticks &&
        total_loops < max_loops)
        failure = "chunk limit before sufficient elapsed time";
done:
    Permit();
    if (calibration_ticks)
        debug("  bench: Dhrystone calibration: %lu loops in %lu us\n",
              calibration_loops,
              (ULONG)((uint64_t)calibration_ticks * 1000000 / frequency));
    if (failure) {
        debug("  bench: Dhrystone timing rejected: %s\n", (ULONG)failure);
        debug("    chunk %lu, loops %lu, ticks %lu, EClock %lu Hz\n",
              chunks + 1, loops, ticks, frequency);
        return 0;
    }
    debug("  bench: finished Dhrystone with %lu chunks and %lu loops in %lu us\n",
          chunks, total_loops, (ULONG)(total_ticks * 1000000 / frequency));
    debug("    chunk time min/max: %lu/%lu us, target %lu us\n",
          (ULONG)((uint64_t)shortest * 1000000 / frequency),
          (ULONG)((uint64_t)longest * 1000000 / frequency),
          (ULONG)((uint64_t)target_ticks * 1000000 / frequency));
    uint64_t result = (uint64_t)total_loops * frequency / total_ticks;
    return result > ULONG_MAX ? ULONG_MAX : (ULONG)result;
}

/* Run the original Dhrystone 2.1 benchmark. */
ULONG run_dhrystone(void)
{
    const ULONG default_loops = 1000UL;
    const uint64_t min_runtime_us = 2000000ULL; /* Aim for ~2 seconds to reduce timer noise */
    const uint64_t max_loops = 5000000ULL;      /* Upper bound from the original sources */
    const int max_attempts = 3;
    ULONG loops = default_loops;
    ULONG E_Freq;
    struct EClockVal start, end;
    uint64_t elapsed = 0;
    int attempt;

    if (!benchmark_timer_available()) return 0;

    if (g_debug_enabled) {
        debug_dhrystone_location("Dhry_Run", (APTR)Dhry_Run);
        debug_dhrystone_location("Proc_1", (APTR)Proc_1);
        debug_dhrystone_location("Proc_6", (APTR)Proc_6);
        debug_dhrystone_location("Func_2", (APTR)Func_2);
        debug("  bench: Dhrystone I-cache %lu, D-cache %lu, "
              "I-burst %lu, D-burst %lu\n",
              (ULONG)hw_info.icache_enabled, (ULONG)hw_info.dcache_enabled,
              (ULONG)hw_info.iburst_enabled, (ULONG)hw_info.dburst_enabled);
    }

    if (frequency_eclock_available())
        return run_dhrystone_chunks();

    for (attempt = 0; attempt < max_attempts; attempt++) {
        if (!Dhry_Initialize()) {
            return 0;
        }
        Forbid();
        E_Freq = read_benchmark_clock(&start);
        Dhry_Run(loops);
        E_Freq = read_benchmark_clock(&end);
        Permit();
        elapsed = EClock_Diff_in_ms(&start, &end, E_Freq);

        if (elapsed >= min_runtime_us || loops >= max_loops) {
            break;
        }

        if (elapsed < 100) { // super fast system
            loops *= 16;
            if (loops > max_loops) {
                loops = max_loops;
            }
        } else {
            unsigned long long scaled_loops = (min_runtime_us * (uint64_t)loops / elapsed) + (uint64_t)loops;
            if (scaled_loops <= (uint64_t)loops) {
                scaled_loops = (2ULL) * (uint64_t)loops; //double the loops
            }
            if (scaled_loops > max_loops) {
                scaled_loops = max_loops;
            }
            loops = (ULONG)scaled_loops;
        }
    }

    debug("  bench: finished Dhrystone with %lu attempts and %lu loops in %lu us\n", attempt, loops, (ULONG)elapsed);
    if (elapsed == 0) {
        return 0;
    }

    {
        uint64_t dhrystones_per_sec =
            ((uint64_t)loops * 1000000ULL) / elapsed;

        if (dhrystones_per_sec > ULONG_MAX) {
            return ULONG_MAX;
        }
        return (ULONG)dhrystones_per_sec;
    }
}

/*
 * Calculate MIPS from Dhrystones
 * Based on VAX 11/780 reference (1757 Dhrystones = 1 MIPS)
 */
ULONG calculate_mips(ULONG dhrystones)
{
    unsigned long long scaled = (unsigned long long)dhrystones * 100ULL;
    scaled /= 1757ULL;
    if (scaled > ULONG_MAX) {
        return ULONG_MAX;
    }
    return (ULONG)scaled;
}

/*
 * Run MFLOPS benchmark (floating point)
 */
ULONG run_mflops_benchmark(void)
{
    struct EClockVal start, end;
    ULONG E_Freq;
    uint64_t elapsed = 0;
    ULONG iterations = 50000;
    ULONG multiplier;
    ULONG fpu;

    /* Check if FPU is available */
    if (hw_info.fpu_type == FPU_NONE) {
        debug("  bench: no fpu!\n");
        return 0;
    }

    switch (hw_info.fpu_type)
        {
        case FPU_NONE:
            return 0;
        case FPU_68881:
            fpu = ASM_FPU_68881;
            break;
        case FPU_68882:
            fpu = ASM_FPU_68882;
            break;
        case FPU_68040:
            fpu = ASM_FPU_68040;
            break;
        case FPU_68060:
            fpu = ASM_FPU_68060;
            break;
        case FPU_68080:
            fpu = ASM_FPU_68080;
            break;
        case FPU_UNKNOWN:
        default:
            debug("  bench: unknown fpu!\n");
            return 0;
        }

    if (!benchmark_timer_available()) {
        debug("  bench: no timer!\n");
        return 0;
    }

    for (multiplier = 1; multiplier <= MAX_MULTIPLY && elapsed < MIN_FLOP_MEASURE; multiplier++)
    {
        iterations = FLOPS_BASE_LOOPS * multiplier;
        Forbid();
        E_Freq = read_benchmark_clock(&start);
        DoFlops(iterations, fpu);
        E_Freq = read_benchmark_clock(&end);
        Permit();
        elapsed = EClock_Diff_in_ms(&start, &end, E_Freq);
    }
    debug("  bench: flops elapsed: %lu, loops %lu\n", (ULONG)elapsed, iterations);

    /* Calculate MFLOPS * 100 using integer math */
    if (elapsed > 0) {
        ULONG total_ops = (iterations * FLOP_LOOP_INSTRUCTIONS) + FLOP_INIT_INSTRUCTIONS;
        unsigned long long scaled =
            (unsigned long long)total_ops * 100ULL;
        scaled /= elapsed; /* ops per microsecond = MFLOPS */
        if (scaled > ULONG_MAX) {
            return ULONG_MAX;
        }
        return (ULONG)scaled;
    }

    return 0;
}

/*
 * Measure memory read speed for a given address range
 * Returns speed in bytes per second
 */
/*
 * Measure loop overhead for compensation
 */
ULONG measure_loop_overhead(ULONG count)
{

    if (!benchmark_timer_available() || count == 0)
        return 0;
    ULONG E_Freq;
    struct EClockVal start, end;

    Forbid();
    E_Freq = read_benchmark_clock(&start);
    __asm__ volatile(
        "1: subq.l #1,%0\n\t"
        "bne.s 1b"
        : "+d"(count)
        :
        : "cc");

    E_Freq = read_benchmark_clock(&end);
    Permit();
    return EClock_Diff_in_ms(&start, &end, E_Freq);
}

static ULONG measure_mem_read_speed_once(volatile ULONG *src, ULONG buffer_size,
                                         ULONG iterations, ULONG *elapsed_us)
{
    ULONG E_Freq, end_freq;
    struct EClockVal start, end;
    uint64_t elapsed;
    ULONG overhead;
    uint64_t total_read;
    uint64_t total_loops;
    ULONG longs_per_read;
    ULONG loop_count;
    ULONG i;
    volatile ULONG *aligned_src;
    uint64_t speed;

    /* Ensure buffer is large enough for our unrolled loop */
    if (!TimerBase) return 0;

    /* Align source pointer to 16 bytes for optimal burst mode */
    aligned_src = (volatile ULONG *)(((ULONG)src + 15) & ~15);
    if ((ULONG)aligned_src < (ULONG)src) return 0;

    /* Adjust buffer size if alignment reduced available space */
    if ((ULONG)aligned_src > (ULONG)src) {
        ULONG diff = (ULONG)aligned_src - (ULONG)src;
        if (buffer_size > diff) buffer_size -= diff;
        else buffer_size = 0;
    }

    longs_per_read = buffer_size / sizeof(ULONG);
    loop_count = longs_per_read / 32; /* 8 regs * 4 unrolls = 32 longs (128 bytes) per iter */

    if (loop_count == 0) return 0;

    /* Only complete 128-byte blocks are read; the aligned buffer may
     * still have an unused tail. Keep accounting outside the timed loop. */
    total_loops = (uint64_t)loop_count * iterations;
    if (total_loops == 0 || total_loops > ULONG_MAX) return 0;
    total_read = total_loops * 32 * sizeof(ULONG);

    Forbid();
    E_Freq = read_benchmark_clock(&start);

    for (i = 0; i < iterations; i++) {
        volatile ULONG *p = aligned_src;
        ULONG count = loop_count;

        /* ASM loop: 4x unrolled movem.l (8 regs) = 128 bytes per loop iteration
         * Matches 'bustest' implementation for maximum bus saturation.
         */
        __asm__ volatile (
            "1:\n\t"
            "movem.l (%0)+,%%d1-%%d4/%%a1-%%a4\n\t"
            "movem.l (%0)+,%%d1-%%d4/%%a1-%%a4\n\t"
            "movem.l (%0)+,%%d1-%%d4/%%a1-%%a4\n\t"
            "movem.l (%0)+,%%d1-%%d4/%%a1-%%a4\n\t"
            "subq.l #1,%1\n\t"
            "bne.s 1b"
            : "+a" (p), "+d" (count)
            :
            : "d1", "d2", "d3", "d4", "a1", "a2", "a3", "a4", "cc", "memory"
        );
    }
    end_freq = read_benchmark_clock(&end);
    Permit();
    if (end_freq != E_Freq || end.ev_hi < start.ev_hi ||
        (end.ev_hi == start.ev_hi && end.ev_lo < start.ev_lo))
        return 0;
    elapsed = EClock_Diff_in_ms(&start, &end, E_Freq);


    /* Compensate for loop overhead */
    overhead = measure_loop_overhead((ULONG)total_loops);
    if (elapsed > overhead) {
        elapsed -= overhead;
    } else {
        /* A non-positive corrected interval cannot yield a valid speed. */
        debug("  bench: memory interval %lu us does not exceed "
              "loop overhead %lu us\n", (ULONG)elapsed, overhead);
        return 0;
    }

    if (elapsed > 0 && total_read > 0) {
        if (elapsed_us) *elapsed_us = (ULONG)elapsed;
        speed = (total_read * 1000000ULL) / elapsed;
        if (speed > ULONG_MAX) return ULONG_MAX;
        return (ULONG)speed;
    }

    return 0;
}

/*
 * Measure memory read speed for a given address range
 * Returns speed in bytes per second
 */
ULONG measure_mem_read_speed(volatile ULONG *src, ULONG buffer_size,
                             ULONG iterations)
{
    ULONG elapsed_us = 0;
    ULONG test_iterations;
    ULONG speed;

    test_iterations = iterations ? iterations : 1;
    speed = measure_mem_read_speed_once(src, buffer_size, test_iterations,
                                        &elapsed_us);

    while (speed >= MEM_SPEED_FAST_THRESHOLD &&
           elapsed_us < MEM_SPEED_TARGET_US &&
           test_iterations < MEM_SPEED_MAX_ITERATIONS) {
        uint64_t next_iterations;

        if (elapsed_us > 0) {
            next_iterations = ((uint64_t)MEM_SPEED_TARGET_US *
                               test_iterations) / elapsed_us;
            next_iterations += test_iterations;
        } else {
            next_iterations = (uint64_t)test_iterations * 2;
        }

        if (next_iterations <= test_iterations)
            next_iterations = (uint64_t)test_iterations * 2;
        if (next_iterations > MEM_SPEED_MAX_ITERATIONS)
            next_iterations = MEM_SPEED_MAX_ITERATIONS;
        if (next_iterations == test_iterations)
            break;

        debug("  bench: memory speed %lu B/s in %lu us, retrying with "
              "%lu iterations\n",
              speed, elapsed_us, (ULONG)next_iterations);

        test_iterations = (ULONG)next_iterations;
        elapsed_us = 0;
        speed = measure_mem_read_speed_once(src, buffer_size, test_iterations,
                                            &elapsed_us);
    }

    return speed;
}

/*
 * Helper to test RAM speed by allocating a buffer
 */
static ULONG test_ram_speed(ULONG mem_flags, ULONG buffer_size, ULONG iterations)
{
    APTR buffer;
    ULONG speed = 0;

    buffer = AllocMem(buffer_size, mem_flags | MEMF_CLEAR);
    if (buffer) {
        speed = measure_mem_read_speed(
            (volatile ULONG *)buffer, buffer_size, iterations);
        FreeMem(buffer, buffer_size);
    }
    return speed;
}

/*
 * Run memory speed tests for CHIP, FAST, and ROM
 * Results stored in bench_results
 */
void run_memory_speed_tests(void)
{
    ULONG buffer_size = 65536;
    ULONG iterations = 128;

    /* Test CHIP RAM speed */
    bench_results.chip_speed = test_ram_speed(MEMF_CHIP, buffer_size, iterations);

    /* Test FAST RAM speed (if available) */
    bench_results.fast_speed = test_ram_speed(MEMF_FAST, buffer_size, iterations);

    /* Test ROM read speed (Kickstart ROM at $F80000) */
    bench_results.rom_speed = measure_mem_read_speed(
        (volatile ULONG *)0xF80000, buffer_size, iterations);
}

static ULONG cpu_frequency_config(void)
{
    refresh_cache_status();
    return ((ULONG)hw_info.cpu_type << 8) |
           (!!hw_info.icache_enabled << 0) |
           (!!hw_info.dcache_enabled << 1) |
           (!!hw_info.iburst_enabled << 2) |
           (!!hw_info.dburst_enabled << 3) |
           (!!hw_info.copyback_enabled << 4) |
           (!!hw_info.super_scalar_enabled << 5) |
           (!!hw_info.mmu_enabled << 6) |
           (frequency_eclock_available() << 7);
}

void measure_processor_frequencies(void)
{
    static ULONG previous_config, previous_mhz;
    ULONG config = cpu_frequency_config();
    ULONG mhz;

    debug("  bench: calc cpu frequency...\n");
    mhz = get_mhz_cpu();
    if (cpu_frequency_config() != config) {
        /* A setting changed during the measurement; neither result applies. */
        mhz = previous_mhz = 0;
        debug("    cpu_mhz: configuration changed during measurement\n");
    } else if (mhz) {
        previous_config = config;
        previous_mhz = mhz;
    } else if (previous_mhz && previous_config == config) {
        mhz = previous_mhz;
        debug("    cpu_mhz: retaining %lu MHz/100, configuration $%08lx\n",
              mhz, config);
    } else {
        previous_mhz = 0;
        debug("    cpu_mhz: unavailable, no matching previous reading\n");
    }
    hw_info.cpu_mhz = mhz;
    debug("  bench: calc fpu frequency...\n");
    hw_info.fpu_mhz = hw_info.fpu_enabled ? get_mhz_fpu() : 0;
}

/*
 * Run all benchmarks
 */
void run_benchmarks(void)
{
    //clear last results
    memset(&bench_results, 0, sizeof(bench_results));

    debug("  bench: run dhrystone...\n");
    /* Run Dhrystone */
    bench_results.dhrystones = run_dhrystone();

    /* Calculate MIPS */
    debug("  bench: run mips...\n");
    bench_results.mips = calculate_mips(bench_results.dhrystones);

    /* Run MFLOPS if FPU available */
    if (hw_info.fpu_type != FPU_NONE) {
        debug("  bench: run mflops...\n");
        //attention! an unpatched 68040 crashes here!
        if (hw_info.fpu_enabled) {
            bench_results.mflops = run_mflops_benchmark();
        }
        else {
            debug("  bench: 68040/060: missing 68040/060.library. Cannot compute flops!\n");
        }
    }

    /* Run memory speed tests (CHIP, FAST, ROM) */
    debug("  bench: run ram/rom speed...\n");
    run_memory_speed_tests();

    measure_processor_frequencies();

    bench_results.benchmarks_valid = TRUE;
    generate_comment();
}

/*
 * Get maximum Dhrystones value (for bar graph scaling)
 */
ULONG get_max_dhrystones(void)
{
    ULONG max_val = 0;
    int i;

    /* Check reference systems */
    for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
        if (reference_systems[i].dhrystones > max_val) {
            max_val = reference_systems[i].dhrystones;
        }
    }

    /* Check current system */
    if (bench_results.benchmarks_valid &&
        bench_results.dhrystones > max_val) {
        max_val = bench_results.dhrystones;
    }

    /* Ensure we have a reasonable minimum */
    if (max_val < 1000) max_val = 1000;

    return max_val;
}

/*
 * Generate a comment based on system configuration
 */
void generate_comment(void)
{
    const char *comment;
    if (bench_results.benchmarks_valid) {
        comment = get_string(MSG_COMMENT_DEFAULT); // slower than a stock A500!

        if (bench_results.dhrystones > 980) {
            comment = get_string(MSG_COMMENT_CLASSIC);
        }
        if (bench_results.dhrystones > 1300) {
            comment = get_string(MSG_COMMENT_GOOD);
        }
        if (bench_results.dhrystones > 2000) { // 68020@14 MHz should be here
            comment = get_string(MSG_COMMENT_FAST);
        }
        if (bench_results.dhrystones > 7000) { // 68030@25 MHz should be here
            comment = get_string(MSG_COMMENT_VERY_FAST);
        }
        if (bench_results.dhrystones > 30000) { // 68040@25 MHz should be here
            comment = get_string(MSG_COMMENT_BLAZING);
        }
        if (bench_results.dhrystones > 80000) { // 68060@ 50 MHz should be here
            comment = get_string(MSG_COMMENT_RIDICULOUS);
        }
        if (bench_results.dhrystones > 130000) { // 68060>75Mhz should be here
            comment = get_string(MSG_COMMENT_LUDICROUS);
        }
        if (bench_results.dhrystones > 200000) { // this is more than a 68060@100MHz -> "new CPU"
            comment = get_string(MSG_COMMENT_WARP11);
        }
    } else {
        comment = get_string(MSG_NA);
    }

    copy_string(hw_info.comment, comment, sizeof(hw_info.comment));
}

ULONG EClock_Diff_in_ms(struct EClockVal *start, struct EClockVal *end, ULONG EFreq)
{
    uint64_t elapsed;

    if (EFreq > 0) {
        elapsed = (((((uint64_t)end->ev_hi << 32) + (uint64_t)end->ev_lo) -
                    (((uint64_t)start->ev_hi << 32) + (uint64_t)start->ev_lo))) *
                   (uint64_t)1000000;
        elapsed /= EFreq;
    } else {
        uint64_t start_us = ((uint64_t)start->ev_hi * 1000000ULL) +
                            (uint64_t)start->ev_lo;
        uint64_t end_us = ((uint64_t)end->ev_hi * 1000000ULL) +
                          (uint64_t)end->ev_lo;
        elapsed = end_us - start_us;
    }

    return (ULONG)elapsed;
}
