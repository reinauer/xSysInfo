// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Print/export results to file
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <dos/dos.h>
#include <dos/datetime.h>

#include <proto/dos.h>

#include "xsysinfo.h"
#include "print.h"
#include "hardware.h"
#include "wdprobe.h"
#include "software.h"
#include "benchmark.h"
#include "memory.h"
#include "boards.h"
#include "drives.h"
#include "locale_str.h"

/* External references */
extern HardwareInfo hw_info;
extern BenchmarkResults bench_results;
extern SoftwareList libraries_list;
extern SoftwareList devices_list;
extern SoftwareList resources_list;
extern SoftwareList mmu_list;
extern MemoryRegionList memory_regions;
extern BoardList board_list;
extern DriveList drive_list;

static BOOL export_write_failed;

static void write_export_bytes(BPTR fh, const char *str, LONG len)
{
    if (len <= 0) {
        return;
    }

    if (Write(fh, (const char *)str, len) != len) {
        export_write_failed = TRUE;
    }
}

/* Helper macro for writing to file */
#define WRITE_LINE(fh, str) do { \
    write_export_bytes(fh, (const char *)str, \
                       (LONG)strlen((const char *)str)); \
    write_export_bytes(fh, (const char *)"\n", 1); \
} while (0)

/*
 * Write a formatted line to file
 */
static void write_formatted(BPTR fh, const char *format, ...)
{
    char buffer[256];
    va_list args;

    va_start(args, format);
    if (vsnprintf(buffer, sizeof(buffer), format, args) < 0) {
        buffer[0] = '\0';
        export_write_failed = TRUE;
    }
    va_end(args);

    WRITE_LINE(fh, (STRPTR)buffer);
}

static const char *on_off_string(BOOL enabled)
{
    return enabled ? "On" : "Off";
}

static const char *yes_no_string(BOOL enabled)
{
    return enabled ? "Yes" : "No";
}

static void format_speed_display(ULONG speed, char *buffer, ULONG size)
{
    if (speed >= 1000000) {
        snprintf(buffer, size, "%lu.%lu MB/s",
                 (unsigned long)(speed / 1000000),
                 (unsigned long)((speed % 1000000) / 100000));
    } else if (speed >= 10000) {
        snprintf(buffer, size, "%lu.%lu KB/s",
                 (unsigned long)(speed / 1000),
                 (unsigned long)((speed % 1000) / 100));
    } else if (speed > 0) {
        snprintf(buffer, size, "%lu B/s", (unsigned long)speed);
    } else {
        snprintf(buffer, size, "---");
    }
}

static void format_mmu_address(char *buffer, ULONG size, ULONG address)
{
    APTR phys = mmu_physical_address((APTR)address);

    if (phys != (APTR)address) {
        snprintf(buffer, size, "$%08lX ->%s", (unsigned long)address,
                 get_location_string(determine_mem_location(phys)));
    } else {
        snprintf(buffer, size, "$%08lX", (unsigned long)address);
    }
}

static void format_paula_string(char *buffer, ULONG size)
{
    switch (hw_info.paula_type) {
    case PAULA_ORIG:
        snprintf(buffer, size, "%s", get_string(MSG_PAULA_ORIG));
        break;
    case PAULA_SAGA:
        snprintf(buffer, size, "%s (ID: %02X)",
                 get_string(MSG_PAULA_SAGA), hw_info.paula_rev);
        break;
    case PAULA_UNKNOWN:
    default:
        snprintf(buffer, size, "%s (ID: %02X)",
                 get_string(MSG_PAULA_UNKNOWN), hw_info.paula_rev);
        break;
    }
}

static void format_fpu_string(char *buffer, ULONG size)
{
    if (hw_info.fpu_type != FPU_NONE && hw_info.fpu_mhz > 0) {
        char mhz_buf[16];

        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.fpu_mhz, TRUE);
        if (hw_info.fpu_enabled) {
            snprintf(buffer, size, "%s %s MHz",
                     hw_info.fpu_string, mhz_buf);
        } else {
            snprintf(buffer, size, "%s %s MHz (Off)",
                     hw_info.fpu_string, mhz_buf);
        }
    } else if (hw_info.fpu_enabled || hw_info.fpu_type == FPU_NONE) {
        snprintf(buffer, size, "%s", hw_info.fpu_string);
    } else {
        snprintf(buffer, size, "%s (Off)", hw_info.fpu_string);
    }
}

static void format_gary_string(char *buffer, ULONG size)
{
    switch (hw_info.gary_type) {
    case GARY_A1000:
        snprintf(buffer, size, "%s", get_string(MSG_GARY_A1000));
        break;
    case GARY_A500:
        snprintf(buffer, size, "%s", get_string(MSG_GARY_A500));
        break;
    case GAYLE:
        snprintf(buffer, size, "%s %02X",
                 get_string(MSG_GAYLE), hw_info.gary_rev);
        break;
    case FAT_GARY:
        snprintf(buffer, size, "%s", get_string(MSG_FAT_GARY));
        break;
    case GARY_UNKNOWN:
    default:
        snprintf(buffer, size, "%s", get_string(MSG_GARY_UNKNOWN));
        break;
    }
}

static void format_ramsey_refresh(char *buffer, ULONG size)
{
    switch (hw_info.ramsey_refresh_rate) {
    case 0:
        snprintf(buffer, size, "156 clk");
        break;
    case 1:
        snprintf(buffer, size, "240 clk");
        break;
    case 2:
        snprintf(buffer, size, "372 clk");
        break;
    default:
        snprintf(buffer, size, "Off");
        break;
    }
}

/*
 * Export header with date/time
 */
void export_header(BPTR fh)
{
    char date_str[32]; date_str[0] = '\0';
    char time_str[32]; time_str[0] = '\0';
#ifndef __KICK13__
    struct DateTime dt;

    DateStamp(&dt.dat_Stamp);
    dt.dat_Format = FORMAT_DOS;
    dt.dat_Flags = 0;
    dt.dat_StrDay = NULL;
    dt.dat_StrDate = (STRPTR)date_str;
    dt.dat_StrTime = (STRPTR)time_str;

    DateToStr(&dt);
#endif

    WRITE_LINE(fh, "================================================================================");
    WRITE_LINE(fh, "                    " XSYSINFO_NAME " " XSYSINFO_VERSION " System Report");
    WRITE_LINE(fh, "================================================================================");
    write_formatted(fh, "Generated: %s %s", date_str, time_str);
    WRITE_LINE(fh, "");
}

/*
 * Export hardware information
 */
void export_hardware(BPTR fh)
{
    char buffer[74];

    WRITE_LINE(fh, "=== HARDWARE INFORMATION ===");
    WRITE_LINE(fh, "");
    write_formatted(fh, "%-16s %s", "Clock:", hw_info.clock_string);
    write_formatted(fh, "%-16s %s", "Amiga:",
                    hw_info.amiga_model_string);

    /* DMA/Gfx */
    switch (hw_info.agnus_type)
    {
    case AGNUS_OCS_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_OCS_NTSC));
        break;
    case AGNUS_OCS_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_OCS_PAL));
        break;
    case AGNUS_OCS_FAT_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_OCS_FAT_NTSC));
        break;
    case AGNUS_OCS_FAT_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_OCS_FAT_PAL));
        break;
    case AGNUS_ECS_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_NTSC));
        break;
    case AGNUS_ECS_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_PAL));
        break;
    case AGNUS_ECS_2MB_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_2MB_NTSC));
        break;
    case AGNUS_ECS_2MB_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_2MB_PAL));
        break;
    case AGNUS_ECS_B_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_B_NTSC));
        break;
    case AGNUS_ECS_B_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ECS_B_PAL));
        break;
    case AGNUS_ALICE_NTSC:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ALICE_NTSC));
        break;
    case AGNUS_ALICE_PAL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_ALICE_PAL));
        break;
    case AGNUS_SAGA:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_AGNUS_SAGA));
        break;
    case AGNUS_UNKNOWN:
    default:
        snprintf(buffer, sizeof(buffer), "%s %04X",
                 get_string(MSG_AGNUS_UNKNOWN), hw_info.agnus_rev);
        break;
    }
    write_formatted(fh, "%-16s %s", "DMA/Gfx:", buffer);

    write_formatted(fh, "%-16s %s", "Mode:", hw_info.mode_string);

    /* Display */
    switch (hw_info.denise_type)
    {
    case DENISE_OCS:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_DENISE_OCS));
        break;
    case DENISE_ECS:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_DENISE_ECS));
        break;
    case DENISE_LISA:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_DENISE_LISA));
        break;
    case DENISE_ISABEL:
        snprintf(buffer, sizeof(buffer), "%s",
                 get_string(MSG_DENISE_SAGA));
        break;
    case DENISE_UNKNOWN:
    default:
        snprintf(buffer, sizeof(buffer), "%s %02X",
                 get_string(MSG_DENISE_UNKNOWN), hw_info.denise_rev);
        break;
    }
    write_formatted(fh, "%-16s %s", "Display:", buffer);

    format_paula_string(buffer, sizeof(buffer));
    write_formatted(fh, "%-16s %s", "Sound:", buffer);

    {
        char mhz_buf[16];
        char frequency[24] = "";
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz, TRUE);
        if (hw_info.cpu_mhz)
            snprintf(frequency, sizeof(frequency), " %s MHz", mhz_buf);
        if (hw_info.cpu_revision[0] != '\0' &&
            strcmp(hw_info.cpu_revision, "N/A") != 0) {
            snprintf(buffer, sizeof(buffer), "%s (%s)%s",
                     hw_info.cpu_string, hw_info.cpu_revision, frequency);
        } else {
            snprintf(buffer, sizeof(buffer), "%s%s",
                     hw_info.cpu_string, frequency);
        }
    }
    write_formatted(fh, "%-16s %s", "CPU/MHz:", buffer);

    if (hw_info.ramsey_rev) {
        if (hw_info.bus_mhz) {
            format_scaled(buffer, sizeof(buffer), hw_info.bus_mhz, TRUE);
            write_formatted(fh, "%-16s %s MHz", "Motherboard Bus:", buffer);
        } else {
            write_formatted(fh, "%-16s %s", "Motherboard Bus:", get_string(MSG_NA));
        }
    }

    format_fpu_string(buffer, sizeof(buffer));
    write_formatted(fh, "%-16s %s", "FPU:", buffer);

    if (hw_info.mmu_enabled) {
        snprintf(buffer, sizeof(buffer), "%s (In use)", hw_info.mmu_string);
    } else {
        strncpy(buffer, hw_info.mmu_string, sizeof(buffer) - 1);
    }
    write_formatted(fh, "%-16s %s", "MMU:", buffer);

    format_mmu_address(buffer, sizeof(buffer), hw_info.vbr);
    write_formatted(fh, "%-16s %s", "VBR:", buffer);

    format_mmu_address(buffer, sizeof(buffer), hw_info.ssp);
    write_formatted(fh, "%-16s %s", "SSP:", buffer);

    format_mmu_address(buffer, sizeof(buffer), (ULONG)SysBase);
    write_formatted(fh, "%-16s %s", "ExecBase:", buffer);

    format_mmu_address(buffer, sizeof(buffer), 0);
    write_formatted(fh, "%-16s %s", "Page 0:", buffer);

    write_formatted(fh, "%-16s %s", "Comment:", hw_info.comment);

    {
        unsigned long long horiz_khz =
            ((unsigned long long)hw_info.horiz_freq * 100ULL) / 1000ULL;
        format_scaled(buffer, sizeof(buffer), (ULONG)horiz_khz, FALSE);
    }
    write_formatted(fh, "%-16s %s kHz", "Horiz Freq:", buffer);

    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.eclock_freq);
    write_formatted(fh, "%-16s %s Hz", "EClock:", buffer);

    format_ramsey_rev_string(buffer, sizeof(buffer));
    write_formatted(fh, "%-16s %s", "Ramsey Rev:", buffer);

    format_gary_string(buffer, sizeof(buffer));
    write_formatted(fh, "%-16s %s", "Gary Rev:", buffer);

    write_formatted(fh, "%-16s %s", "Card Slot:", hw_info.card_slot_string);

    snprintf(buffer, sizeof(buffer), "%lu Hz", (unsigned long)hw_info.vert_freq);
    write_formatted(fh, "%-16s %s", "Vert Freq:", buffer);

    snprintf(buffer, sizeof(buffer), "%lu Hz", (unsigned long)hw_info.supply_freq);
    write_formatted(fh, "%-16s %s", "Supply Freq:", buffer);

    WRITE_LINE(fh, "");

    /* Cache status */
    WRITE_LINE(fh, "Cache Status:");
    write_formatted(fh, "  ICache:   %s",
                    hw_info.has_icache ? (hw_info.icache_enabled ? "On" : "Off") : "N/A");
    write_formatted(fh, "  DCache:   %s",
                    hw_info.has_dcache ? (hw_info.dcache_enabled ? "On" : "Off") : "N/A");
    write_formatted(fh, "  IBurst:   %s",
                    hw_info.has_iburst ? (hw_info.iburst_enabled ? "On" : "Off") : "N/A");
    write_formatted(fh, "  DBurst:   %s",
                    hw_info.has_dburst ? (hw_info.dburst_enabled ? "On" : "Off") : "N/A");
    write_formatted(fh, "  CopyBack: %s",
                    hw_info.has_copyback ? (hw_info.copyback_enabled ? "On" : "Off") : "N/A");
    write_formatted(fh, "  Super Scalar: %s",
                    hw_info.has_super_scalar ?
                    (hw_info.super_scalar_enabled ? "On" : "Off") : "N/A");

    WRITE_LINE(fh, "");
    WRITE_LINE(fh, "Extended Hardware:");
    format_ramsey_rev_string(buffer, sizeof(buffer));
    write_formatted(fh, "  Ramsey Rev:     %s", buffer);

    if (hw_info.ramsey_rev) {
        write_formatted(fh, "  Ramsey Control:");
        write_formatted(fh, "    Page:         %s",
                        on_off_string(hw_info.ramsey_page_enabled));
        write_formatted(fh, "    Burst:        %s",
                        on_off_string(hw_info.ramsey_burst_enabled));
        write_formatted(fh, "    Wrap:         %s",
                        on_off_string(hw_info.ramsey_wrap_enabled));
        write_formatted(fh, "    RAM size:     %s", get_ramsey_size_string());
        if (hw_info.ramsey_rev == 0x0f)
            write_formatted(fh, "    Skip:         %s",
                            on_off_string(hw_info.ramsey_skip_enabled));
        format_ramsey_refresh(buffer, sizeof(buffer));
        write_formatted(fh, "    Refresh:      %s", buffer);
    }

    WRITE_LINE(fh, "  NV-RAM:");
    if (hw_info.battMemData.valid_data) {
        write_formatted(fh, "    Amnesia:      %s",
                        yes_no_string(hw_info.battMemData.amnesia_amiga));
        write_formatted(fh, "    Shared Amn.:  %s",
                        yes_no_string(hw_info.battMemData.amnesia_shared));
        write_formatted(fh, "    Timeout:      %s",
                        hw_info.battMemData.long_timeout ? "Long" : "Short");
        write_formatted(fh, "    Scan LUN:     %s",
                        on_off_string(hw_info.battMemData.scan_luns));
        write_formatted(fh, "    Sync Trans.:  %s",
                        on_off_string(hw_info.battMemData.sync_transfer));
        write_formatted(fh, "    Fast sync:    %s",
                        on_off_string(hw_info.battMemData.fast_sync_transfer));
        write_formatted(fh, "    Queuing:      %s",
                        on_off_string(hw_info.battMemData.tagged_queuing));
        write_formatted(fh, "    SCSI Host ID: %d",
                        hw_info.battMemData.scsi_id);
    } else {
        WRITE_LINE(fh, "    N/A");
    }

    format_dma_string(buffer, sizeof(buffer));
    write_formatted(fh, "  DMA chip:       %s", buffer);
    if (hw_info.resdmac_version) {
        format_resdmac_version(buffer, sizeof(buffer));
        write_formatted(fh, "    Firmware:     %s", buffer);
    }
    format_scsi_chip_string(buffer, sizeof(buffer));
    write_formatted(fh, "  SCSI chip:      %s", buffer);
    if (hw_info.sdmac_present && wd_info.chip != WD_UNKNOWN) {
        static const char *labels[WD_DETAIL_COUNT] = {
            "Microcode", "SCSI clock", "I/O mode", "Timeout", "Sync / offset"
        };
        unsigned detail;
        for (detail = 0; detail < WD_DETAIL_COUNT; detail++) {
            format_wd_detail(detail, buffer, sizeof(buffer));
            write_formatted(fh, "    %-13s %s", labels[detail], buffer);
        }
        WRITE_LINE(fh, "    Configuration captured before the SCSI check;");
        WRITE_LINE(fh, "    sync setting describes the last selected target.");
    }
    if (hw_info.ncr_type != NCR_NONE) {
        static const char *labels[NCR_DETAIL_COUNT] = {
            "SCSI ID", "DMA burst", "Sync / offset", "Data width",
            "Parity check", "Clock doubler"
        };
        unsigned detail;
        unsigned count = hw_info.ncr_type == NCR_53C770 ?
                         NCR_DETAIL_COUNT : NCR_DETAIL_DOUBLER;
        for (detail = 0; detail < count; detail++) {
            format_ncr_detail(detail, buffer, sizeof(buffer));
            write_formatted(fh, "    %-13s %s", labels[detail], buffer);
        }
    }

    WRITE_LINE(fh, "");
}

/*
 * Export software lists
 */
void export_software(BPTR fh)
{
    ULONG i;

    WRITE_LINE(fh, "=== SYSTEM SOFTWARE ===");
    WRITE_LINE(fh, "");

    /* Libraries */
    WRITE_LINE(fh, "--- Libraries ---");
    write_formatted(fh, "%-20s %-12s %-12s %s", "Name", "Location", "Address", "Version");
    for (i = 0; i < libraries_list.count; i++) {
        SoftwareEntry *e = &libraries_list.entries[i];
        write_formatted(fh, "%-20s %-12s $%08lX   V%d.%d",
                        e->name, get_location_string(e->location),
                        (unsigned long)e->address, e->version, e->revision);
    }
    WRITE_LINE(fh, "");

    /* Devices */
    WRITE_LINE(fh, "--- Devices ---");
    write_formatted(fh, "%-20s %-12s %-12s %s", "Name", "Location", "Address", "Version");
    for (i = 0; i < devices_list.count; i++) {
        SoftwareEntry *e = &devices_list.entries[i];
        write_formatted(fh, "%-20s %-12s $%08lX   V%d.%d",
                        e->name, get_location_string(e->location),
                        (unsigned long)e->address, e->version, e->revision);
    }
    WRITE_LINE(fh, "");

    /* Resources */
    WRITE_LINE(fh, "--- Resources ---");
    write_formatted(fh, "%-20s %-12s %-12s %s", "Name", "Location", "Address", "Version");
    for (i = 0; i < resources_list.count; i++) {
        SoftwareEntry *e = &resources_list.entries[i];
        write_formatted(fh, "%-20s %-12s $%08lX   V%d.%d",
                        e->name, get_location_string(e->location),
                        (unsigned long)e->address, e->version, e->revision);
    }
    WRITE_LINE(fh, "");

    /* MMU entries */
    WRITE_LINE(fh, "--- MMU Entries ---");
    for (i = 0; i < mmu_list.count; i++) {
        SoftwareEntry *e = &mmu_list.entries[i];
        write_formatted(fh, "%s", e->name);
    }
    WRITE_LINE(fh, "");
}

/*
 * Export benchmark results
 */
void export_benchmarks(BPTR fh)
{
    WRITE_LINE(fh, "=== SPEED COMPARISONS ===");
    WRITE_LINE(fh, "");

    if (bench_results.benchmarks_valid) {
        write_formatted(fh, "Dhrystones:        %lu", (unsigned long)bench_results.dhrystones);
        {
            char scaled_buf[16];
            format_scaled(scaled_buf, sizeof(scaled_buf), bench_results.mips, FALSE);
            write_formatted(fh, "MIPS:              %s", scaled_buf);
        }

        if (hw_info.fpu_type != FPU_NONE) {
            char scaled_buf[16];
            format_scaled(scaled_buf, sizeof(scaled_buf), bench_results.mflops, FALSE);
            write_formatted(fh, "MFLOPS:            %s", scaled_buf);
        } else {
            WRITE_LINE(fh, "MFLOPS:            N/A (no FPU)");
        }

        /* Memory speeds */
        {
            char chip_str[16], fast_str[16], rom_str[16];

            if (bench_results.chip_speed > 0) {
                format_scaled(chip_str, sizeof(chip_str), bench_results.chip_speed / 10000, TRUE);
            } else {
                strncpy(chip_str, "N/A", sizeof(chip_str));
            }

            if (bench_results.fast_speed > 0) {
                format_scaled(fast_str, sizeof(fast_str), bench_results.fast_speed / 10000, TRUE);
            } else {
                strncpy(fast_str, "N/A", sizeof(fast_str));
            }

            if (bench_results.rom_speed > 0) {
                format_scaled(rom_str, sizeof(rom_str), bench_results.rom_speed / 10000, TRUE);
            } else {
                strncpy(rom_str, "N/A", sizeof(rom_str));
            }

            write_formatted(fh, "Memory Speed:      Chip %s  Fast %s  ROM %s MB/s",
                           chip_str, fast_str, rom_str);
        }
    } else {
        WRITE_LINE(fh, "Benchmarks not run. Press SPEED button to run benchmarks.");
    }

    WRITE_LINE(fh, "");

    /* Reference systems */
    WRITE_LINE(fh, "Reference Systems:");
    {
        ULONG i;
        for (i = 0; i < NUM_REFERENCE_SYSTEMS; i++) {
            char ref_label[32];
            format_reference_label(ref_label, sizeof(ref_label), &reference_systems[i]);
            write_formatted(fh, "  %s:  %lu Dhrystones",
                            ref_label, (unsigned long)reference_systems[i].dhrystones);
        }
    }

    WRITE_LINE(fh, "");
}

/*
 * Export memory information
 */
void export_memory(BPTR fh)
{
    ULONG i;
    char size_str[32];

    WRITE_LINE(fh, "=== MEMORY ===");
    WRITE_LINE(fh, "");

    for (i = 0; i < memory_regions.count; i++) {
        MemoryRegion *r = &memory_regions.regions[i];
        char speed_str[32];

        refresh_memory_region(i);

        write_formatted(fh, "Region %lu: %s", (unsigned long)(i + 1), r->node_name);
        write_formatted(fh, "  Start:  $%08lX", (unsigned long)r->start_address);
        write_formatted(fh, "  End:    $%08lX", (unsigned long)r->end_address);

        format_size(r->total_size, size_str, sizeof(size_str));
        write_formatted(fh, "  Size:   %s (%lu bytes)", size_str, (unsigned long)r->total_size);

        write_formatted(fh, "  Type:    %s", r->type_string);
        write_formatted(fh, "  Priority: %d", r->priority);
        write_formatted(fh, "  Lower:   $%08lX", (unsigned long)r->lower_bound);
        write_formatted(fh, "  Upper:   $%08lX", (unsigned long)r->upper_bound);
        write_formatted(fh, "  First:   $%08lX", (unsigned long)r->first_free);
        write_formatted(fh, "  Free:    %lu bytes", (unsigned long)r->amount_free);
        write_formatted(fh, "  Largest: %lu bytes", (unsigned long)r->largest_block);
        write_formatted(fh, "  Chunks:  %lu", (unsigned long)r->num_chunks);
        if (r->speed_measured) {
            format_speed_display(r->speed_bytes_sec, speed_str,
                                 sizeof(speed_str));
        } else {
            snprintf(speed_str, sizeof(speed_str), "---");
        }
        write_formatted(fh, "  Speed:   %s", speed_str);
        WRITE_LINE(fh, "");
    }
}

/*
 * Export expansion boards
 */
void export_boards(BPTR fh)
{
    ULONG i;

    WRITE_LINE(fh, "=== EXPANSION BOARDS ===");
    WRITE_LINE(fh, "");

    if (board_list.count == 0) {
        WRITE_LINE(fh, "No expansion boards detected.");
        WRITE_LINE(fh, "");
        return;
    }

    write_formatted(fh, "%-12s %-8s %-10s %-20s %-16s %s",
                    "Address", "Size", "Type", "Product", "Manufacturer", "Serial");

    for (i = 0; i < board_list.count; i++) {
        BoardInfo *b = &board_list.boards[i];

        write_formatted(fh, "$%08lX   %-8s %-10s %-20s %-16s %ld",
                        (unsigned long)b->board_address,
                        b->size_string,
                        get_board_type_string(b->board_type),
                        b->product_name,
                        b->manufacturer_name,
                        (long)b->serial_number);
    }

    WRITE_LINE(fh, "");
}

/*
 * Export drives information
 */
void export_drives(BPTR fh)
{
    ULONG i;

    WRITE_LINE(fh, "=== DRIVES ===");
    WRITE_LINE(fh, "");

    if (drive_list.count == 0) {
        WRITE_LINE(fh, "No drives detected.");
        WRITE_LINE(fh, "");
        return;
    }

    for (i = 0; i < drive_list.count; i++) {
        DriveInfo *d = &drive_list.drives[i];
        char block_size_buffer[64];
        char fs_buffer[64];
        char speed_buffer[32];

        format_block_size_display(d, block_size_buffer,
                                  sizeof(block_size_buffer));
        format_filesystem_display(d, fs_buffer, sizeof(fs_buffer));
        if (d->speed_measured) {
            format_speed_display(d->speed_bytes_sec, speed_buffer,
                                 sizeof(speed_buffer));
        } else {
            snprintf(speed_buffer, sizeof(speed_buffer), "---");
        }

        write_formatted(fh, "Drive: %s", d->device_name);
        write_formatted(fh, "  Volume:      %s",
                        d->volume_name[0] ? d->volume_name : "---");
        write_formatted(fh, "  Handler:     %s",
                        d->handler_name[0] ? d->handler_name : "---");
        write_formatted(fh, "  Disk errors: %lu",
                        (unsigned long)d->disk_errors);
        write_formatted(fh, "  Unit:        %lu", (unsigned long)d->unit_number);
        write_formatted(fh, "  State:       %s", get_disk_state_string(d->disk_state));
        write_formatted(fh, "  Filesystem:  %s", fs_buffer);
        write_formatted(fh, "  Total:       %lu blocks", (unsigned long)d->total_blocks);
        write_formatted(fh, "  Used:        %lu blocks", (unsigned long)d->blocks_used);
        write_formatted(fh, "  Block size:  %s", block_size_buffer);
        write_formatted(fh, "  Surfaces:    %lu",
                        (unsigned long)d->surfaces);
        write_formatted(fh, "  Sectors:     %lu",
                        (unsigned long)d->sectors_per_track);
        write_formatted(fh, "  Reserved:    %lu",
                        (unsigned long)d->reserved_blocks);
        write_formatted(fh, "  Low cyl:     %lu",
                        (unsigned long)d->low_cylinder);
        write_formatted(fh, "  High cyl:    %lu",
                        (unsigned long)d->high_cylinder);
        write_formatted(fh, "  Buffers:     %lu",
                        (unsigned long)d->num_buffers);
        write_formatted(fh, "  Speed:       %s", speed_buffer);

        WRITE_LINE(fh, "");
    }
}

/*
 * Export all information to a DOS file handle
 */
BOOL export_to_handle(BPTR fh)
{
    if (!fh) {
        return FALSE;
    }

    export_write_failed = FALSE;

    export_header(fh);
    export_hardware(fh);
    export_software(fh);
    export_benchmarks(fh);
    export_memory(fh);
    export_boards(fh);
    export_drives(fh);

    WRITE_LINE(fh, "================================================================================");
    WRITE_LINE(fh, "                          End of " XSYSINFO_NAME " Report");
    WRITE_LINE(fh, "================================================================================");

    return !export_write_failed;
}

/*
 * Export all information to file
 */
BOOL export_to_file(const char *filename)
{
    BPTR fh;
    BOOL ok;

    fh = Open((STRPTR)filename, MODE_NEWFILE);
    if (!fh) {
        return FALSE;
    }

    ok = export_to_handle(fh);
    Close(fh);

    return ok;
}
