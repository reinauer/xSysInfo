// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Print/export results to file
 */

#include <string.h>
#include <stdio.h>

#include <dos/dos.h>
#include <dos/datetime.h>

#include <proto/dos.h>

#include "xsysinfo.h"
#include "print.h"
#include "hardware.h"
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
extern MemoryRegionList memory_regions;
extern BoardList board_list;
extern DriveList drive_list;

/* Helper macro for writing to file */
#define WRITE_LINE(fh, str) FPuts(fh, (STRPTR)str); FPuts(fh, (STRPTR)"\n")

/*
 * Write a formatted line to file
 */
static void write_formatted(BPTR fh, const char *format, ...)
{
    char buffer[256];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    FPuts(fh, (STRPTR)buffer);
    FPuts(fh, (STRPTR)"\n");
}

/*
 * Export header with date/time
 */
void export_header(BPTR fh)
{
    struct DateTime dt;
    char date_str[32];
    char time_str[32];

    DateStamp(&dt.dat_Stamp);
    dt.dat_Format = FORMAT_DOS;
    dt.dat_Flags = 0;
    dt.dat_StrDay = NULL;
    dt.dat_StrDate = (STRPTR)date_str;
    dt.dat_StrTime = (STRPTR)time_str;

    DateToStr(&dt);

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

    WRITE_LINE(fh, "=== INTERNAL HARDWARE MODES ===");
    WRITE_LINE(fh, "");
    write_formatted(fh, "%-16s %s", "Clock:", hw_info.clock_string);

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

    if (hw_info.cpu_revision[0] != '\0' &&
        strcmp(hw_info.cpu_revision, "N/A") != 0) {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz, TRUE);
        snprintf(buffer, sizeof(buffer), "%s (%s) %s MHz",
                 hw_info.cpu_string, hw_info.cpu_revision, mhz_buf);
    } else {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.cpu_mhz, TRUE);
        snprintf(buffer, sizeof(buffer), "%s %s MHz",
                 hw_info.cpu_string, mhz_buf);
    }
    write_formatted(fh, "%-16s %s", "CPU/MHz:", buffer);

    if (hw_info.fpu_type != FPU_NONE && hw_info.fpu_mhz > 0) {
        char mhz_buf[16];
        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.fpu_mhz, TRUE);
        snprintf(buffer, sizeof(buffer), "%s %s MHz",
                 hw_info.fpu_string, mhz_buf);
        write_formatted(fh, "%-16s %s", "FPU:", buffer);
    } else {
        write_formatted(fh, "%-16s %s", "FPU:", hw_info.fpu_string);
    }

    if (hw_info.mmu_enabled) {
        snprintf(buffer, sizeof(buffer), "%s (IN USE)", hw_info.mmu_string);
    } else {
        strncpy(buffer, hw_info.mmu_string, sizeof(buffer) - 1);
    }
    write_formatted(fh, "%-16s %s", "MMU:", buffer);

    snprintf(buffer, sizeof(buffer), "$%08lX", (unsigned long)hw_info.vbr);
    write_formatted(fh, "%-16s %s", "VBR:", buffer);

    write_formatted(fh, "%-16s %s", "Comment:", hw_info.comment);

    {
        unsigned long long horiz_khz =
            ((unsigned long long)hw_info.horiz_freq * 100ULL) / 1000ULL;
        format_scaled(buffer, sizeof(buffer), (ULONG)horiz_khz, FALSE);
    }
    write_formatted(fh, "%-16s %s KHz", "Horiz Freq:", buffer);

    snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.eclock_freq);
    write_formatted(fh, "%-16s %s Hz", "EClock:", buffer);

    if (hw_info.ramsey_rev) {
        snprintf(buffer, sizeof(buffer), "%02x", hw_info.ramsey_rev);
    } else {
        strncpy(buffer, "N/A", sizeof(buffer) - 1);
    }
    write_formatted(fh, "%-16s %s", "Ramsey Rev:", buffer);

    if (hw_info.gary_rev) {
        snprintf(buffer, sizeof(buffer), "%02x", hw_info.gary_rev);
    } else {
        strncpy(buffer, "N/A", sizeof(buffer) - 1);
    }
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
                    hw_info.has_icache ? (hw_info.icache_enabled ? "ON" : "OFF") : "N/A");
    write_formatted(fh, "  DCache:   %s",
                    hw_info.has_dcache ? (hw_info.dcache_enabled ? "ON" : "OFF") : "N/A");
    write_formatted(fh, "  IBurst:   %s",
                    hw_info.has_iburst ? (hw_info.iburst_enabled ? "ON" : "OFF") : "N/A");
    write_formatted(fh, "  DBurst:   %s",
                    hw_info.has_dburst ? (hw_info.dburst_enabled ? "ON" : "OFF") : "N/A");
    write_formatted(fh, "  CopyBack: %s",
                    hw_info.has_copyback ? (hw_info.copyback_enabled ? "ON" : "OFF") : "N/A");

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

            write_formatted(fh, "Memory Speed:      CHIP %s  FAST %s  ROM %s MB/s",
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

        write_formatted(fh, "Region %lu: %s", (unsigned long)(i + 1), r->node_name);
        write_formatted(fh, "  Start:  $%08lX", (unsigned long)r->start_address);
        write_formatted(fh, "  End:    $%08lX", (unsigned long)r->end_address);

        format_size(r->total_size, size_str, sizeof(size_str));
        write_formatted(fh, "  Size:   %s (%lu bytes)", size_str, (unsigned long)r->total_size);

        write_formatted(fh, "  Type:   %s", r->type_string);
        write_formatted(fh, "  Free:   %lu bytes", (unsigned long)r->amount_free);
        write_formatted(fh, "  Largest: %lu bytes", (unsigned long)r->largest_block);
        write_formatted(fh, "  Chunks: %lu", (unsigned long)r->num_chunks);
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
        ULONG display_block_size = get_display_block_size(d);

        write_formatted(fh, "Drive: %s", d->device_name);
        write_formatted(fh, "  Volume:      %s",
                        d->volume_name[0] ? d->volume_name : "---");
        write_formatted(fh, "  Handler:     %s",
                        d->handler_name[0] ? d->handler_name : "---");
        write_formatted(fh, "  Unit:        %lu", (unsigned long)d->unit_number);
        write_formatted(fh, "  State:       %s", get_disk_state_string(d->disk_state));
        write_formatted(fh, "  Filesystem:  %s", get_filesystem_string(d->fs_type));
        write_formatted(fh, "  Total:       %lu blocks", (unsigned long)d->total_blocks);
        write_formatted(fh, "  Used:        %lu blocks", (unsigned long)d->blocks_used);
        write_formatted(fh, "  Block size:  %lu bytes", (unsigned long)display_block_size);

        if (d->speed_measured) {
            write_formatted(fh, "  Speed:       %lu bytes/sec", (unsigned long)d->speed_bytes_sec);
        }

        WRITE_LINE(fh, "");
    }
}

/*
 * Export all information to file
 */
BOOL export_to_file(const char *filename)
{
    BPTR fh;

    fh = Open((STRPTR)filename, MODE_NEWFILE);
    if (!fh) {
        return FALSE;
    }

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

    Close(fh);

    return TRUE;
}
