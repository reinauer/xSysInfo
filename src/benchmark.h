/*
 * xSysInfo - Benchmark header
 */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "xsysinfo.h"

/* Reference system data */
typedef struct {
    const char *name;       /* System name (e.g., "A600") */
    const char *cpu;        /* CPU description */
    ULONG mhz;              /* Clock speed */
    ULONG dhrystones;       /* Dhrystone score */
    ULONG mips;             /* MIPS rating * 100 */
    ULONG mflops;           /* MFLOPS rating * 100 (0 if no FPU) */
    ULONG chip_speed;       /* Relative chip RAM speed * 100 (100 = A600) */
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

/* Function prototypes */

/* Run all benchmarks */
void run_benchmarks(void);

/* Individual benchmarks */
ULONG run_dhrystone(void);
ULONG run_mflops_benchmark(void);
void run_memory_speed_tests(void);

/* Helper functions */
ULONG calculate_mips(ULONG dhrystones);
ULONG get_max_dhrystones(void);  /* Returns max of all systems including "You" */

/* Timer functions for benchmarking */
BOOL init_timer(void);
void cleanup_timer(void);
uint64_t get_timer_ticks(void);    /* Returns ticks (1/1000000 sec precision) */
void wait_ticks(ULONG ticks);

#endif /* BENCHMARK_H */
