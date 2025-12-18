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

#include "xsysinfo.h"
#include "benchmark.h"
#include "hardware.h"

/* Global benchmark results */
BenchmarkResults bench_results;

/* Reference system data (placeholder values - to be calibrated); scaled by 100 */
    /* name,  cpu,     mhz,   dhry, mips, mfls */
const ReferenceSystem reference_systems[NUM_REFERENCE_SYSTEMS] = {
    /* A600:  68000 @ 7.09 MHz, no FPU */
    {"A600",  "68000",   7,   1028,   58,    0},
    /* B2000: 68000 @ 7.09 MHz, no FPU */
    {"B2000", "68000",   7,   1028,   58,    0},
    /* A1200: 68EC020 @ 14 MHz, no FPU */
    {"A1200", "EC020",  14,   2550,  145,    0},
    /* A2500: 68020 @ 14 MHz */
    {"A2500", "68020",  14,   2100,  120,    0},
    /* A3000: 68030 / 68882 @ 25 MHz */
    {"A3000", "68030",  25,   7090,  403,  285},
    /* A4000: 68040 @ 25 MHz, internal FPU */
    {"A4000", "68040",  25,  20530, 1168,  578},
};

void format_reference_label(char *buffer, size_t buffer_size, const ReferenceSystem *ref)
{
    if (!buffer || buffer_size == 0 || !ref) return;

    snprintf(buffer, buffer_size, "%-5s %-5s %luMHz",
             ref->name, ref->cpu, (unsigned long)ref->mhz);
}

/* Timer resources */
static struct MsgPort *timer_port = NULL;
static struct timerequest *timer_req = NULL;
struct Device *TimerBase = NULL;
static BOOL timer_open = FALSE;

/* External references */
extern struct ExecBase *SysBase;
extern HardwareInfo hw_info;

/* Dhrystone implementation (from original source) */
int Dhry_Initialize(void);
void Dhry_Run(unsigned long Number_Of_Runs);

/*
 * Initialize timer for benchmarking
 */
BOOL init_timer(void)
{
    timer_port = CreateMsgPort();
    if (!timer_port) return FALSE;

    timer_req = (struct timerequest *)
        CreateIORequest(timer_port, sizeof(struct timerequest));
    if (!timer_req) {
        DeleteMsgPort(timer_port);
        timer_port = NULL;
        return FALSE;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
                   (struct IORequest *)timer_req, 0) != 0) {
        DeleteIORequest((struct IORequest *)timer_req);
        DeleteMsgPort(timer_port);
        timer_req = NULL;
        timer_port = NULL;
        return FALSE;
    }

    TimerBase = (struct Device *)timer_req->tr_node.io_Device;
    timer_open = TRUE;

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
        DeleteIORequest((struct IORequest *)timer_req);
        timer_req = NULL;
    }

    if (timer_port) {
        DeleteMsgPort(timer_port);
        timer_port = NULL;
    }

    TimerBase = NULL;
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
    const ULONG default_loops = 20000UL;
    const ULONG min_runtime_us = 2000000UL; /* Aim for ~2 seconds to reduce timer noise */
    const ULONG max_loops = 5000000UL;      /* Upper bound from the original sources */
    const int max_attempts = 3;
    ULONG loops = default_loops;
    ULONG elapsed = 0;
    int attempt;

    if (!TimerBase) return 0;

    for (attempt = 0; attempt < max_attempts; attempt++) {
        if (!Dhry_Initialize()) {
            return 0;
        }

        {
            ULONG start_time = get_timer_ticks();
            Dhry_Run(loops);
            elapsed = get_timer_ticks() - start_time;
        }

        if (elapsed >= min_runtime_us || loops >= max_loops) {
            break;
        }

        if (elapsed == 0) {
            loops *= 10;
        } else {
            unsigned long long scaled_loops =
                ((unsigned long long)loops * (unsigned long long)min_runtime_us) /
                (unsigned long long)elapsed;
            if (scaled_loops < (unsigned long long)loops) {
                scaled_loops = loops + 1;
            } else {
                scaled_loops += 1ULL;
            }
            if (scaled_loops > max_loops) {
                scaled_loops = max_loops;
            }
            loops = (ULONG)scaled_loops;
        }
    }

    if (elapsed == 0) {
        return 0;
    }

    {
        unsigned long long dhrystones_per_sec =
            ((unsigned long long)loops * 1000000ULL) /
            (unsigned long long)elapsed;

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
    ULONG start_time, end_time, elapsed;
    ULONG iterations = 50000;
    ULONG ops_per_iter = 8;  /* FP operations per iteration */
    ULONG i;

    /* Check if FPU is available */
    if (hw_info.fpu_type == FPU_NONE) {
        return 0;
    }

    if (!TimerBase) return 0;

    start_time = get_timer_ticks();

    __asm__ volatile (
        "fmove.l  #2,fp0\n\t"         /* fb = 2 */
        "fmove.l  #3,fp1\n\t"         /* fc = 3 */
        "fmove.l  #4,fp2\n\t"         /* fd = 4 */
        :
        :
        : "fp0", "fp1", "fp2", "cc", "memory"
    );
    for (i = 0; i < iterations; i++) {
        /* Inline 68881+/040/060 FPU sequence; no C-side FP state needed */
        __asm__ volatile (
            "fadd.x   fp1,fp0\n\t"        /* fa = fb + fc */
            "fmul.x   fp2,fp0\n\t"        /* fb = fa * fd */
            "fsub.x   fp1,fp0\n\t"        /* fc = fb - fa */
            "fdiv.x   fp2,fp0\n\t"        /* fd = fc / fb */
            "fmul.x   fp1,fp0\n\t"        /* fa = fb * fc */
            "fadd.x   fp2,fp0\n\t"        /* fb = fa + fd */
            "fsub.x   fp1,fp0\n\t"        /* fc = fb - fa */
            "fmul.x   fp2,fp0\n\t"        /* fd = fc * fa */
            :
            :
            : "fp0", "fp1", "fp2", "cc", "memory"
        );
    }

    end_time = get_timer_ticks();
    elapsed = end_time - start_time;

    /* Calculate MFLOPS * 100 using integer math */
    if (elapsed > 0) {
        ULONG total_ops = iterations * ops_per_iter;
        unsigned long long scaled =
            (unsigned long long)total_ops * 100ULL;
        scaled /= (unsigned long long)elapsed; /* ops per microsecond = MFLOPS */
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
    ULONG start, end;

    if (!TimerBase || count == 0) return 0;

    start = get_timer_ticks();

    __asm__ volatile (
        "1: subq.l #1,%0\n\t"
        "bne.s 1b"
        : "+d" (count)
        :
        : "cc"
    );

    end = get_timer_ticks();
    return end - start;
}

/*
 * Measure memory read speed for a given address range
 * Returns speed in bytes per second
 */
ULONG measure_mem_read_speed(volatile ULONG *src, ULONG buffer_size, ULONG iterations)
{
    ULONG start_time, end_time, elapsed, overhead;
    ULONG total_read = 0;
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

    start_time = get_timer_ticks();

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
    }

    end_time = get_timer_ticks();

    elapsed = end_time - start_time;

    /* Compensate for loop overhead */
    /* Total loops executed = iterations * loop_count */
    overhead = measure_loop_overhead(iterations * loop_count);
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
    ULONG iterations = 16;

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
    memset(&bench_results, 0, sizeof(bench_results));

    /* Run Dhrystone */
    bench_results.dhrystones = run_dhrystone();

    /* Calculate MIPS */
    bench_results.mips = calculate_mips(bench_results.dhrystones);

    /* Run MFLOPS if FPU available */
    if (hw_info.fpu_type != FPU_NONE) {
        bench_results.mflops = run_mflops_benchmark();
    }

    /* Run memory speed tests (CHIP, FAST, ROM) */
    run_memory_speed_tests();

    bench_results.benchmarks_valid = TRUE;
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
