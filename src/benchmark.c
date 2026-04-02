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

#include <proto/exec.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>

#include "xsysinfo.h"
#include "benchmark.h"
#include "hardware.h"
#include "debug.h"
#include "cpu.h"
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

/* Dhrystone implementation (from original source) */
int Dhry_Initialize(void);
void Dhry_Run(unsigned long Number_Of_Runs);

/*
 * Initialize timer for benchmarking
 */
BOOL init_timer(void)
{
    timer_port = CreatePort(NULL, 0);
    if (!timer_port) return FALSE;

    etimer_port = CreatePort(NULL, 0);
    if (!etimer_port) {
        DeletePort(timer_port);
        return FALSE;
    }

    timer_req = (struct timerequest *)
        CreateExtIO(timer_port, sizeof(struct timerequest));
    if (!timer_req) {
        debug("    init_timer: no timer_req\n");
        DeletePort(timer_port);
        DeletePort(etimer_port);
        timer_port = NULL;
        return FALSE;
    }
    etimer_req = (struct timerequest *)
        CreateExtIO(etimer_port, sizeof(struct timerequest));
    if (!etimer_req) {
        debug("    init_timer: no etimer_req\n");
        DeletePort(timer_port);
        DeletePort(etimer_port);
        timer_port = NULL;
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
                   (struct IORequest *)timer_req, 0) != 0) {
        debug("    init_timer: no OpenDevice timer_req\n");
        DeleteExtIO((struct IORequest *)timer_req);
        DeletePort(timer_port);
        DeletePort(etimer_port);
        timer_req = NULL;
        timer_port = NULL;
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK,
                   (struct IORequest *)etimer_req, 0) != 0) {
        debug("    init_timer: no OpenDevice etimer_req\n");
        DeleteExtIO((struct IORequest *)timer_req);
        DeleteExtIO((struct IORequest *)etimer_req);
        DeletePort(timer_port);
        DeletePort(etimer_port);
        timer_req = NULL;
        etimer_req = NULL;
        timer_port = NULL;
        return FALSE;
    }

    TimerBase = (struct Device *)timer_req->tr_node.io_Device;
    timer_open = TRUE;

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

/*
 * Returns the CPU-frequencies in MHz scaled by 100
*/
ULONG get_mhz_cpu()
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
        count = (uint64_t) measure_loop_overhead(loop); //this counts the speed of looping
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
            //see if we run in fastram or chipram (huge difference in speed calc!)
            test = __builtin_return_address(0); // this gets the return address, which tells me if we are running in fast ram
            // see if it is fast mem!
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
        case CPU_68030:
        case CPU_68EC030:
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
                    count *=50; //without super scalar the cpu seems 2 times slower!
                }
            }
            else {
                count *=20; //without mmu the 68060 seems 5 times slower!
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
 * Returns the FPU-frequencies in MHz
*/
ULONG get_mhz_fpu()
{

    /*make some sanity tests:
    No FPU -> nothing,
    Unknown FPU -> nothing
    68EC/LC040/060 -> No FPU nothing
    68040 or 68060 CPU: Same as CPU Frequency
    */

    if (FPU_NONE == hw_info.fpu_type || FPU_UNKNOWN == hw_info.fpu_type)
    {
        return 0;
    }

    if (!ETimerBase) return 0;

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
        // cpu-frequency available?
        if (hw_info.cpu_mhz == 0)
        {
            get_mhz_cpu(); // recalc
        }
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
        Forbid();
        E_Freq =ReadEClock(&start);
        __asm__ volatile(
            "fmove.w #1,fp1\n\t"
            "1:\t\tfdiv.x fp1,fp1\n\t"
            "subq.l\t#1,%0\n\t"
            "bne.s\t1b\n\t"
            : "+d"(loop)
            :
            : "cc", "fp1");

        E_Freq =ReadEClock(&end);
        Permit();
        loop = FPULOOPS * multiplier; //the above inlineassembly modifies loop

        count = (uint64_t) EClock_Diff_in_ms(&start, &end, E_Freq);

        overhead = measure_loop_overhead(loop);
        if (count > overhead) {
            count -= (uint64_t) overhead;
        }
        if (multiplier >= MAX_MULTIPLY || count >= MIN_MHZ_MEASURE) {
            break;
        }
    }

    tmp = BASE_FACTOR * (uint64_t) multiplier;
    debug("    fpu_mhz: results: %llu %llu %lu\n", count, tmp, overhead);

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

/*
 * Run the original Dhrystone 2.1 benchmark
 */
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

    if (!ETimerBase) return 0;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        if (!Dhry_Initialize()) {
            return 0;
        }
        Forbid();
        E_Freq = ReadEClock(&start);
        Dhry_Run(loops);
        E_Freq = ReadEClock(&end);
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

    if (!ETimerBase) {
        debug("  bench: no timer!\n");
        return 0;
    }

    for (multiplier = 1; multiplier <= MAX_MULTIPLY && elapsed < MIN_FLOP_MEASURE; multiplier++)
    {
        iterations = FLOPS_BASE_LOOPS * multiplier;
        Forbid();
        E_Freq = ReadEClock(&start);
        DoFlops(iterations, fpu);
        E_Freq = ReadEClock(&end);
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

    if (!ETimerBase || count == 0)
        return 0;
    ULONG E_Freq, oldCount;
    struct EClockVal start, end;

    Forbid();
    E_Freq = ReadEClock(&start);
    __asm__ volatile(
        "1: subq.l #1,%0\n\t"
        "bne.s 1b"
        : "+d"(count)
        :
        : "cc");

    E_Freq = ReadEClock(&end);
    Permit();
    count = oldCount;
    return EClock_Diff_in_ms(&start, &end, E_Freq);
}

/*
 * Measure memory read speed for a given address range
 * Returns speed in bytes per second
 */
ULONG measure_mem_read_speed(volatile ULONG *src, ULONG buffer_size, ULONG iterations)
{
    ULONG E_Freq;
    struct EClockVal start, end;
    uint64_t elapsed;
    ULONG overhead;
    ULONG total_read = 0, total_loops = 0;
    ULONG longs_per_read;
    ULONG loop_count;
    ULONG i;
    volatile ULONG *aligned_src;

    /* Ensure buffer is large enough for our unrolled loop */
    if (!TimerBase) return 0;

    /* Align source pointer to 16 bytes for optimal burst mode */
    aligned_src = (volatile ULONG *)(((ULONG)src + 15) & ~15);

    /* Adjust buffer size if alignment reduced available space */
    if ((ULONG)aligned_src > (ULONG)src) {
        ULONG diff = (ULONG)aligned_src - (ULONG)src;
        if (buffer_size > diff) buffer_size -= diff;
        else buffer_size = 0;
    }

    longs_per_read = buffer_size / sizeof(ULONG);
    loop_count = longs_per_read / 32; /* 8 regs * 4 unrolls = 32 longs (128 bytes) per iter */

    if (loop_count == 0) return 0;

    Forbid();
    E_Freq = ReadEClock(&start);

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
        total_read += buffer_size;
        total_loops += loop_count;
    }
    E_Freq = ReadEClock(&end);
    Permit();
    elapsed = EClock_Diff_in_ms(&start, &end, E_Freq);


    /* Compensate for loop overhead */
    overhead = measure_loop_overhead(total_loops);
    if (elapsed > overhead) {
        elapsed -= overhead;
    } else {
        /* Should not happen, but safety first */
        elapsed = 1;
    }

    if (elapsed > 0 && total_read > 0) {
        return (ULONG)(((uint64_t)total_read * 1000000ULL) / elapsed);
    }

    return 0;
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

    debug("  bench: calc cpu frequency...\n");
    hw_info.cpu_mhz = get_mhz_cpu();
    debug("  bench: calc fpu frequency...\n");
    hw_info.fpu_mhz = get_mhz_fpu();

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
        comment = get_string(MSG_COMMENT_DEFAULT); //slower than a stock A500!

        if (bench_results.dhrystones > 980) {
            comment = get_string(MSG_COMMENT_CLASSIC);
        }
        if (bench_results.dhrystones > 1300) {
            comment = get_string(MSG_COMMENT_GOOD);
        }
        if (bench_results.dhrystones > 2000) { //68020@14 MHz should be here
            comment = get_string(MSG_COMMENT_FAST);
        }
        if (bench_results.dhrystones > 7000) { //68030@25 MHz should be here
            comment = get_string(MSG_COMMENT_VERY_FAST);
        }
        if (bench_results.dhrystones > 30000) { //68040@25 MHz should be here
            comment = get_string(MSG_COMMENT_BLAZING);
        }
        if (bench_results.dhrystones > 80000) { //68060@ 50 MHz should be here
            comment = get_string(MSG_COMMENT_RIDICULUS);
        }
        if (bench_results.dhrystones > 130000) { //68060>75Mhz should be here
            comment = get_string(MSG_COMMENT_LUDICROUS);
        }
        if (bench_results.dhrystones > 200000) { //this is more than a 68060@100MHz -> "new CPU"
            comment = get_string(MSG_COMMENT_WARP11);
        }
    } else {
        comment = get_string(MSG_NA);
    }

    strncpy(hw_info.comment, comment, sizeof(hw_info.comment) - 1);
}

ULONG EClock_Diff_in_ms(struct EClockVal *start, struct EClockVal *end, ULONG EFreq)
{
    uint64_t elapsed;
    elapsed = (((((uint64_t)end->ev_hi << 32) + (uint64_t)end->ev_lo) - (((uint64_t)start->ev_hi << 32) + (uint64_t)start->ev_lo))) * (uint64_t)1000000;
    elapsed /= EFreq; //this should put the value in range of ULONG again
    return (ULONG)elapsed;
}
