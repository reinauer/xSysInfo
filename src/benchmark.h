// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Benchmark header
 */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "xsysinfo.h"
#include "hardware.h"
#include <devices/timer.h>
#include <proto/timer.h>

/* Reference system data */
typedef struct {
    const char *name;       /* System name (e.g., "A600") */
    const char *cpu;        /* CPU description */
    ULONG mhz;              /* Clock speed */
    ULONG dhrystones;       /* Dhrystone score */
    ULONG mips;             /* MIPS rating * 100 */
    ULONG mflops;           /* MFLOPS rating * 100 (0 if no FPU) */
} ReferenceSystem;

/* Number of reference systems */
#define NUM_REFERENCE_SYSTEMS   6

/* Reference system indices */
#define REF_A600    0
#define REF_B2000   1
#define REF_A1200   2
#define REF_A2500   3
#define REF_A3000   4
#define REF_A4000   5
#define MAX_MULTIPLY 1000
#define MIN_MHZ_MEASURE 2000
#define CPULOOPS 14680
#define FPULOOPS 1200
#define BASE_FACTOR 136000
#define FLOPS_BASE_LOOPS 50000
#define MIN_FLOP_MEASURE 4000
#define FLOP_LOOP_INSTRUCTIONS 9
#define FLOP_INIT_INSTRUCTIONS 3

/* Benchmark results */
typedef struct {
    ULONG dhrystones;       /* Dhrystones per second */
    ULONG mips;             /* MIPS rating * 100 */
    ULONG mflops;           /* MFLOPS rating * 100 */
    ULONG chip_speed;       /* Chip RAM speed in bytes/sec */
    ULONG fast_speed;       /* Fast RAM speed in bytes/sec (0 if no fast RAM) */
    ULONG rom_speed;        /* ROM read speed in bytes/sec */
    BOOL benchmarks_valid;  /* TRUE if benchmarks have been run */
} BenchmarkResults;

/* Global benchmark results */
extern BenchmarkResults bench_results;

/* Global reference data */
extern const ReferenceSystem reference_systems[NUM_REFERENCE_SYSTEMS];

/* Format reference system label ("A600  68000  7MHz") */
void format_reference_label(char *buffer, size_t buffer_size, const ReferenceSystem *ref);

/* Function prototypes */

/* Run all benchmarks */
void run_benchmarks(void);

/* Individual benchmarks */
ULONG run_dhrystone(void);
ULONG run_mflops_benchmark(void);
void run_memory_speed_tests(void);
ULONG measure_mem_read_speed(volatile ULONG *src, ULONG buffer_size, ULONG iterations);
ULONG get_mhz_cpu();
ULONG get_mhz_fpu();

/* Helper functions */
ULONG calculate_mips(ULONG dhrystones);
ULONG get_max_dhrystones(void);  /* Returns max of all systems including "You" */

/* Timer functions for benchmarking */
BOOL init_timer(void);
void cleanup_timer(void);
uint64_t get_timer_ticks(void);    /* Returns ticks (1/1000000 sec precision) */
void get_timer(struct timeval *tv);    /* Returns result in provided timeval-structure */
void wait_ticks(ULONG ticks);
ULONG measure_loop_overhead(ULONG count);
void generate_comment(void);
ULONG EClock_Diff_in_ms(struct EClockVal *start, struct EClockVal *end, ULONG EFreq);

#endif /* BENCHMARK_H */
