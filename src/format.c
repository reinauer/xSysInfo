// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors

/* Values and hardware rows shared by the graphical frontends.
 * No screen, widget, or layout state belongs here. */
#include <string.h>
#include <libraries/configregs.h>
#include "format.h"
#include "hardware.h"
#include "software.h"
#include "memory.h"
#include "benchmark.h"
#include "clock.h"
#include "wdprobe.h"

void format_scaled(char *buffer, size_t size, ULONG value_x100, BOOL round)
{
    ULONG integer_part = value_x100 / 100;
    ULONG frac_part = value_x100 % 100;
    if (round && integer_part >= 100) {
        /* Round up if fractional part >= 0.5 */
        if (frac_part >= 50) {
            integer_part++;
        }
        snprintf(buffer, size, "%lu", (unsigned long)integer_part);
    } else {
        snprintf(buffer, size, "%lu.%02lu",
                 (unsigned long)integer_part,
                 (unsigned long)frac_part);
    }
}

static void format_cpu_value(char *buffer, size_t size)
{
    char mhz_buf[16] = "";

    if (hw_info.cpu_mhz > 0) {
        mhz_buf[0] = ' ';
        format_scaled(mhz_buf + 1, sizeof(mhz_buf) - 1,
                      hw_info.cpu_mhz, FALSE);
    }

    if (hw_info.cpu_revision[0] != '\0' &&
        strcmp(hw_info.cpu_revision, "N/A") != 0) {
        snprintf(buffer, size, "%s (%s)%s",
                 hw_info.cpu_string, hw_info.cpu_revision, mhz_buf);
    } else {
        snprintf(buffer, size, "%s%s", hw_info.cpu_string, mhz_buf);
    }
}

static void format_fpu_value(char *buffer, size_t size)
{
    if (hw_info.fpu_type != FPU_NONE && !hw_info.fpu_enabled) {
        snprintf(buffer, size, "%s (%s)",
                 hw_info.fpu_string, get_string(MSG_OFF));
    } else if (hw_info.fpu_type != FPU_NONE &&
               hw_info.fpu_type != FPU_UNKNOWN && hw_info.fpu_mhz > 0) {
        char mhz_buf[16];

        format_scaled(mhz_buf, sizeof(mhz_buf), hw_info.fpu_mhz, FALSE);
        snprintf(buffer, size, "%s %s", hw_info.fpu_string, mhz_buf);
    } else {
        snprintf(buffer, size, "%s", hw_info.fpu_string);
    }
}

static void format_mmu_value(char *buffer, size_t size)
{
    char mmu_value[sizeof(hw_info.mmu_string)];
    const char *uncertainty = strstr(hw_info.mmu_string, " (");

    /* detect_mmu() adds a localized uncertainty suffix; keep it compact here. */
    if (uncertainty) {
        snprintf(mmu_value, sizeof(mmu_value), "%.*s?",
                 (int)(uncertainty - hw_info.mmu_string), hw_info.mmu_string);
    } else {
        copy_string(mmu_value, hw_info.mmu_string, sizeof(mmu_value));
    }

    if (hw_info.mmu_enabled) {
        snprintf(buffer, size, "%s (%s)",
                 mmu_value, get_string(MSG_IN_USE));
    } else {
        copy_string(buffer, mmu_value, size);
    }
}

static void format_mmu_address(char *buffer, size_t size, ULONG address)
{
    APTR phys = mmu_physical_address((APTR)address);

    if (phys != (APTR)address) {
        snprintf(buffer, size, "$%08lX ->%s", (unsigned long)address,
                 get_location_string(determine_mem_location(phys)));
    } else {
        snprintf(buffer, size, "$%08lX", (unsigned long)address);
    }
}

void format_clock_values(char values[2][24])
{
    struct ClockData date;
    if (read_hardware_time(&date)) {
        snprintf(values[0], 24, "%04u-%02u-%02u", date.year, date.month, date.mday);
        snprintf(values[1], 24, "%02u:%02u:%02u", date.hour, date.min, date.sec);
    } else {
        copy_string(values[0], get_string(MSG_NA), 24);
        copy_string(values[1], get_string(MSG_NA), 24);
    }
}

static void format_agnus_value(char *buffer, size_t size)
{
    switch (hw_info.agnus_type) {
        case AGNUS_OCS_NTSC:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_OCS_NTSC));
            break;
        case AGNUS_OCS_PAL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_OCS_PAL));
            break;
        case AGNUS_OCS_FAT_NTSC:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_OCS_FAT_NTSC));
            break;
        case AGNUS_OCS_FAT_PAL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_OCS_FAT_PAL));
            break;
        case AGNUS_ECS_2MB_NTSC:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_2MB_NTSC));
            break;
        case AGNUS_ECS_2MB_PAL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_2MB_PAL));
            break;
        case AGNUS_ECS_NTSC:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_NTSC));
            break;
        case AGNUS_ECS_B_NTSC:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_B_NTSC));
            break;
        case AGNUS_ECS_PAL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_PAL));
            break;
        case AGNUS_ECS_B_PAL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_ECS_B_PAL));
            break;
        case AGNUS_ALICE_NTSC:
            snprintf(buffer, size, "%s Rev. %X",
                  get_string(MSG_AGNUS_ALICE_NTSC), (hw_info.agnus_rev&0xF));
            break;
        case AGNUS_ALICE_PAL:
            snprintf(buffer, size, "%s Rev. %X",
                  get_string(MSG_AGNUS_ALICE_PAL), (hw_info.agnus_rev&0xF));
            break;
        case AGNUS_SAGA:
            snprintf(buffer, size, "%s",
                  get_string(MSG_AGNUS_SAGA));
            break;
        case AGNUS_UNKNOWN:
        default:
            snprintf(buffer, size, "%s %2X",
                  get_string(MSG_AGNUS_UNKNOWN), hw_info.agnus_rev);
            break;
    }
}

static void format_denise_value(char *buffer, size_t size)
{
    switch (hw_info.denise_type) {
        case DENISE_OCS:
            snprintf(buffer, size, "%s",
                  get_string(MSG_DENISE_OCS));
            break;
        case DENISE_ECS:
            snprintf(buffer, size, "%s",
                  get_string(MSG_DENISE_ECS));
            break;
        case DENISE_LISA:
            snprintf(buffer, size, "%s",
                  get_string(MSG_DENISE_LISA));
            break;
        case DENISE_ISABEL:
            snprintf(buffer, size, "%s",
                  get_string(MSG_DENISE_SAGA));
            break;
        case DENISE_UNKNOWN:
        default:
            snprintf(buffer, size, "%s %02X",
                  get_string(MSG_DENISE_UNKNOWN), hw_info.denise_rev);
            break;
    }
}

static void format_paula_value(char *buffer, size_t size)
{
    switch (hw_info.paula_type) {
        case PAULA_ORIG:
            snprintf(buffer, size, "%s",
                  get_string(MSG_PAULA_ORIG));
            break;
        case PAULA_SAGA:
            snprintf(buffer, size, "%s %02X",
                  get_string(MSG_PAULA_SAGA),hw_info.paula_rev);
            break;
        case PAULA_UNKNOWN:
            snprintf(buffer, size, "%s %02X",
                  get_string(MSG_PAULA_UNKNOWN),hw_info.paula_rev);
            break;
    }
}

static void format_gary_value(char *buffer, size_t size)
{
    switch (hw_info.gary_type) {
        case GARY_A1000:
            copy_string(buffer, get_string(MSG_GARY_A1000), size);
            break;
        case GARY_A500:
            copy_string(buffer, get_string(MSG_GARY_A500), size);
            break;
        case GAYLE:
            snprintf(buffer, size, "%s %02X", get_string(MSG_GAYLE), hw_info.gary_rev);
            break;
        case FAT_GARY:
            copy_string(buffer, get_string(MSG_FAT_GARY), size);
            break;
        case GARY_UNKNOWN:
        default:
            copy_string(buffer, get_string(MSG_GARY_UNKNOWN), size);
            break;
    }
}

static const LocaleStringID software_labels[] = {
    MSG_OPERATING_SYSTEM, MSG_ROM, MSG_ACTIVE_ROM, MSG_WORKBENCH,
    MSG_SETPATCH, MSG_GRAPHICS_SYSTEM, MSG_PPC_RUNTIME
};

ULONG software_overview_count(void)
{
    return hw_info.ppc_runtime[0] ? 7 : 6;
}

LocaleStringID software_overview_label(ULONG row)
{
    return row < 7 ? software_labels[row] : MSG_NA;
}

const char *format_software_overview_value(ULONG row, char *buffer,
                                           size_t size)
{
    switch (row) {
    case 0:
        return system_software.os_name[0] ? system_software.os_name :
               get_string(MSG_UNKNOWN_OS);
    case 1:
        snprintf(buffer, size, "%u.%u (%lu KB)",
                 hw_info.kickstart_version, hw_info.kickstart_revision,
                 (unsigned long)hw_info.kickstart_size);
        return buffer;
    case 2:
        snprintf(buffer, size, "%u.%u",
                 hw_info.kickstart_patch_version,
                 hw_info.kickstart_patch_revision);
        return buffer;
    case 3:
        if (!system_software.has_workbench_version)
            return get_string(MSG_NA);
        snprintf(buffer, size, "%u.%u",
                 system_software.workbench_version,
                 system_software.workbench_revision);
        return buffer;
    case 4:
        if (!system_software.has_setpatch_version)
            return get_string(MSG_NA);
        if (system_software.is_tinysetpatch)
            snprintf(buffer, size, "%u.%u (TinySetPatch %u.%u)",
                     system_software.setpatch_version,
                     system_software.setpatch_revision,
                     system_software.tinysetpatch_version,
                     system_software.tinysetpatch_revision);
        else
            snprintf(buffer, size, "%u.%u",
                     system_software.setpatch_version,
                     system_software.setpatch_revision);
        return buffer;
    case 5:
        return system_software.graphics_system;
    default:
        return hw_info.ppc_runtime;
    }
}

ULONG scale_speed_value(ULONG value, ULONG max_value, ULONG extent,
                        BarScale scale)
{
    if (max_value == 0 || value == 0) return 0;

    if (scale == SCALE_EXPAND) {
        /* Linear scale */
        return (ULONG)(((unsigned long long)value * extent) / max_value);
    } else {
        /* Shrink mode: A3000 at 100% */
        ULONG a4000_value = reference_systems[REF_A3000].dhrystones;
        ULONG ref_width = extent;
        if (value <= a4000_value) {
            return (ULONG)(((unsigned long long)value * ref_width) / a4000_value);
        }
        return ref_width +
            (ULONG)(((unsigned long long)(value - a4000_value) * ref_width) /
            (max_value - a4000_value));
    }
}

ULONG speed_scale_max(BarScale scale)
{
    ULONG reference = reference_systems[REF_A4000].dhrystones;
    return scale == SCALE_EXPAND ? get_max_dhrystones() :
                                  (reference ? reference * 2 : 1);
}

typedef struct {
    HardwareRowVisitor visit;
    void *data;
    UWORD group;
} HardwareOutput;

static void hardware_row(HardwareOutput *out, BOOL detail,
                         const char *label, const char *value)
{
    HardwareInfoRow row = { label, value, out->group, detail, CACHE_NONE };
    out->visit(&row, out->data);
}

static void hardware_cache_row(HardwareOutput *out, LocaleStringID label,
                               CacheSetting control, BOOL present)
{
    HardwareInfoRow row = { get_string(label), NULL, out->group,
                           FALSE, control };
    if (present) out->visit(&row, out->data);
}

void visit_hardware_rows(HardwareType page, HardwareRowVisitor visit,
                         void *data)
{
    char buffer[74];

    HardwareOutput out = { visit, data, 0 };
    if (page == HARDWARE_STD) {
        /* Identify the machine before listing its components. */
        hardware_row(&out, FALSE, "Amiga", hw_info.amiga_model_string);

        /* Mode */
        hardware_row(&out, FALSE, get_string(MSG_MODE), hw_info.mode_string);

        /* DMA/Gfx */
        format_agnus_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DMA_GFX), buffer);

        /* Display */

        format_denise_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DISPLAY), buffer);

        /* Sound */
        format_paula_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_SOUND_SYSTEM), buffer);

        /* Ramsey */
        format_ramsey_rev_string(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_RAM_CONTROLLER), buffer);

        /* Gary */
        format_gary_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DECODING), buffer);

        /* CPU/MHz */
        format_cpu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_CPU_MHZ), buffer);

        /* FPU */
        format_fpu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_FPU), buffer);

        /* MMU */
        format_mmu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_MMU), buffer);

        /* Comment */
        hardware_row(&out, FALSE, get_string(MSG_COMMENT), hw_info.comment);

        if (hw_info.ramsey_rev) {
            if (hw_info.bus_mhz)
                format_scaled(buffer, sizeof(buffer), hw_info.bus_mhz, TRUE);
            else
                copy_string(buffer, get_string(MSG_NA), sizeof(buffer));
            hardware_row(&out, FALSE, get_string(MSG_BUS_MHZ), buffer);

        }

        /* Frequencies - left column continues */
        {
            unsigned long long horiz_khz =
                ((unsigned long long)hw_info.horiz_freq * 100ULL) / 1000ULL;
            format_scaled(buffer, sizeof(buffer), (ULONG)horiz_khz, FALSE);
        }
        hardware_row(&out, FALSE, get_string(MSG_HORIZ_KHZ), buffer);

        /* EClock */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.eclock_freq);
        hardware_row(&out, FALSE, get_string(MSG_ECLOCK_HZ), buffer);

        /* Vert Hz */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.vert_freq);
        hardware_row(&out, FALSE, get_string(MSG_VERT_HZ), buffer);

        /* Supply Hz */
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)hw_info.supply_freq);
        hardware_row(&out, FALSE, get_string(MSG_SUPPLY_HZ), buffer);

        /* Card Slot */
        hardware_row(&out, FALSE, get_string(MSG_CARD_SLOT), hw_info.card_slot_string);

        format_size(memory_regions.total_chip_size, buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_CHIP_RAM), buffer);

        format_size(memory_regions.total_fast_size, buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_FAST_RAM), buffer);

    } else if (page == HARDWARE_CPU) {

        format_cpu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_CPU_MHZ), buffer);

        format_fpu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_FPU), buffer);

        format_mmu_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_MMU), buffer);

        if (hw_info.ppc_present) {
            char clock[16] = "";
            out.group++;
            if (hw_info.ppc_mhz) {
                clock[0] = ' ';
                format_scaled(clock + 1, sizeof(clock) - 1, hw_info.ppc_mhz, FALSE);
            }
            snprintf(buffer, sizeof(buffer), "%s%s", hw_info.ppc_string, clock);
            hardware_row(&out, FALSE, "PPC/MHz", buffer);
            snprintf(buffer, sizeof(buffer), "$%04lX", hw_info.ppc_revision);
            hardware_row(&out, FALSE, get_string(MSG_PPC_REVISION), buffer);
            if (hw_info.ppc_bus_mhz) format_scaled(buffer, sizeof(buffer), hw_info.ppc_bus_mhz, FALSE);
            else copy_string(buffer, get_string(MSG_NA), sizeof(buffer));
            hardware_row(&out, FALSE, get_string(MSG_PPC_BUS), buffer);
        }
        out.group++;
        format_mmu_address(buffer, sizeof(buffer), hw_info.vbr);
        hardware_row(&out, FALSE, get_string(MSG_VBR), buffer);

        format_mmu_address(buffer, sizeof(buffer), hw_info.ssp);
        hardware_row(&out, FALSE, get_string(MSG_SSP), buffer);

        format_mmu_address(buffer, sizeof(buffer), (ULONG)SysBase);
        hardware_row(&out, FALSE, "ExecBase", buffer);

        format_mmu_address(buffer, sizeof(buffer), 0);
        hardware_row(&out, FALSE, "Page 0", buffer);

        out.group++;
        hardware_cache_row(&out, MSG_ICACHE, CACHE_ICACHE, hw_info.has_icache);
        hardware_cache_row(&out, MSG_DCACHE, CACHE_DCACHE, hw_info.has_dcache);
        hardware_cache_row(&out, MSG_IBURST, CACHE_IBURST, hw_info.has_iburst);
        hardware_cache_row(&out, MSG_DBURST, CACHE_DBURST, hw_info.has_dburst);
        hardware_cache_row(&out, MSG_CBACK, CACHE_CBACK, hw_info.has_copyback);
        hardware_cache_row(&out, MSG_SUPER_SCALAR, CACHE_SUPER_SCALAR, hw_info.has_super_scalar);
    } else if (page == HARDWARE_EXT) {
        format_agnus_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DMA_GFX), buffer);
        if (hw_info.agnus_type != AGNUS_SAGA) {
            snprintf(buffer, sizeof(buffer), "$%02X", hw_info.agnus_rev);
            hardware_row(&out, TRUE, get_string(MSG_CHIP_ID), buffer);
        }
        out.group++;
        format_denise_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DISPLAY), buffer);
        /* Original Denise has no readable ID register. */
        if (hw_info.denise_type != DENISE_OCS) {
            snprintf(buffer, sizeof(buffer), "$%02X", hw_info.denise_rev);
            hardware_row(&out, TRUE, get_string(MSG_CHIP_ID), buffer);
        }
        out.group++;
        format_paula_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_SOUND_SYSTEM), buffer);
        out.group++;
        format_gary_value(buffer, sizeof(buffer));
        hardware_row(&out, FALSE, get_string(MSG_DECODING), buffer);
        if (hw_info.gayle_pcmcia_valid) {
            static const UWORD access_ns[] = {250, 150, 100, 720};
            hardware_row(&out, TRUE, get_string(MSG_PCMCIA_CARD), get_string((hw_info.gayle_pcmcia_status & 0x40) ? MSG_YES : MSG_NO));
            hardware_row(&out, TRUE, get_string(MSG_PCMCIA_SLOT), get_string((hw_info.gayle_pcmcia_status & 1) ? MSG_OFF : MSG_ON));
            snprintf(buffer, sizeof(buffer), "%u ns", access_ns[(hw_info.gayle_pcmcia_config >> 2) & 3]);
            hardware_row(&out, TRUE, get_string(MSG_PCMCIA_ACCESS), buffer);
        }
        if (hw_info.akiko_present) {
            out.group++;
            snprintf(buffer, sizeof(buffer), "$%08lX", hw_info.akiko_id);
            hardware_row(&out, FALSE, "Akiko", buffer);
            hardware_row(&out, TRUE, get_string(MSG_AKIKO_C2P), get_string(hw_info.akiko_c2p_enabled ? MSG_ON : MSG_OFF));
        }
        out.group++;
        if (hw_info.ramsey_rev) {
            format_ramsey_rev_string(buffer, sizeof(buffer));
            hardware_row(&out, FALSE, get_string(MSG_RAM_CONTROLLER), buffer);
            out.group++;
            /* Ramsey status */
            hardware_row(&out, FALSE, get_string(MSG_RAMSEY_CTRL), NULL);

            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_page_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            hardware_row(&out, TRUE, get_string(MSG_RAMSEY_PAGE), buffer);

            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_burst_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            hardware_row(&out, TRUE, get_string(MSG_RAMSEY_BURST), buffer);

            snprintf(buffer, sizeof(buffer), "%s", hw_info.ramsey_wrap_enabled ? get_string(MSG_ON) : get_string(MSG_OFF));
            hardware_row(&out, TRUE, get_string(MSG_RAMSEY_WRAP), buffer);

            hardware_row(&out, TRUE, get_string(MSG_RAMSEY_SIZE), get_ramsey_size_string());

            if (hw_info.ramsey_rev == 0x0f) {
                hardware_row(&out, TRUE, get_string(MSG_RAMSEY_SKIP), get_string(hw_info.ramsey_skip_enabled ?
                                            MSG_ON : MSG_OFF));

            }
            switch (hw_info.ramsey_refresh_rate) {
                case 0:
                    copy_string(buffer, "156 clk", sizeof(buffer));
                    break;
                case 1:
                    copy_string(buffer, "240 clk", sizeof(buffer));
                    break;
                case 2:
                    copy_string(buffer, "372 clk", sizeof(buffer));
                    break;
                default:
                    copy_string(buffer, get_string(MSG_OFF), sizeof(buffer));
                    break;
               }
            hardware_row(&out, TRUE, get_string(MSG_RAMSEY_REFRESH), buffer);

        }
    } else if (page == HARDWARE_SCSI) {
        if (hw_info.sdmac_present || hw_info.ncr_type != NCR_NONE) {
            format_dma_string(buffer, sizeof(buffer));
            hardware_row(&out, FALSE, get_string(MSG_DMA_CHIP), buffer);
        }
        if (hw_info.resdmac_version) {
            format_resdmac_version(buffer, sizeof(buffer));
            hardware_row(&out, TRUE, get_string(MSG_DMA_VERSION), buffer);

        }
        out.group++;
        if (hw_info.sdmac_present || hw_info.ncr_type != NCR_NONE) {
            format_scsi_chip_string(buffer, sizeof(buffer));
            hardware_row(&out, FALSE, get_string(MSG_SDMAC_REV), buffer);
        }

        if (hw_info.sdmac_present && wd_info.chip != WD_UNKNOWN) {
            static const LocaleStringID labels[WD_DETAIL_COUNT] = {
                MSG_WD_MICROCODE, MSG_WD_CLOCK, MSG_WD_MODE,
                MSG_TIMEOUT, MSG_WD_SYNC_OFFSET
            };
            unsigned detail;
            for (detail = 0; detail < WD_DETAIL_COUNT; detail++) {
                format_wd_detail(detail, buffer, sizeof(buffer));
                hardware_row(&out, TRUE, get_string(labels[detail]), buffer);

            }
        }
        if (hw_info.ncr_type != NCR_NONE) {
            static const LocaleStringID labels[NCR_DETAIL_COUNT] = {
                MSG_SCSI_HOST_ID, MSG_NCR_DMA_BURST, MSG_WD_SYNC_OFFSET,
                MSG_NCR_WIDTH, MSG_NCR_PARITY, MSG_NCR_DOUBLER
            };
            unsigned detail;
            unsigned count = hw_info.ncr_type == NCR_53C770 ?
                             NCR_DETAIL_COUNT : NCR_DETAIL_DOUBLER;
            for (detail = 0; detail < count; detail++) {
                format_ncr_detail(detail, buffer, sizeof(buffer));
                hardware_row(&out, TRUE, get_string(labels[detail]), buffer);

            }
        }
    } else if (page == HARDWARE_CLOCK) {
        hardware_row(&out, FALSE, get_string(MSG_CLOCK), hw_info.clock_string);

        {
            char values[2][24];
            format_clock_values(values);
            hardware_row(&out, FALSE, get_string(MSG_RTC_DATE), values[0]);
            hardware_row(&out, FALSE, get_string(MSG_RTC_TIME), values[1]);
        }
        out.group++;

        if (hw_info.battMemData.available) {
            hardware_row(&out, FALSE, get_string(MSG_NV_RAM), NULL);

            if (hw_info.battMemData.valid_data) {
                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.amnesia_amiga ? get_string(MSG_YES) : get_string(MSG_NO));
                hardware_row(&out, TRUE, get_string(MSG_AMNESIA), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.amnesia_shared ? get_string(MSG_YES) : get_string(MSG_NO));
                hardware_row(&out, TRUE, get_string(MSG_SHARED_AMNESIA), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.long_timeout ? get_string(MSG_LONG) : get_string(MSG_SHORT));
                hardware_row(&out, TRUE, get_string(MSG_TIMEOUT), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.scan_luns ? get_string(MSG_ON) : get_string(MSG_OFF));
                hardware_row(&out, TRUE, get_string(MSG_SCAN_LUN), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.sync_transfer ? get_string(MSG_ON) : get_string(MSG_OFF));
                hardware_row(&out, TRUE, get_string(MSG_SYNC_TRANS), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.fast_sync_transfer ? get_string(MSG_ON) : get_string(MSG_OFF));
                hardware_row(&out, TRUE, get_string(MSG_FAST_SYNC), buffer);

                snprintf(buffer, sizeof(buffer), "%s", hw_info.battMemData.tagged_queuing ? get_string(MSG_ON) : get_string(MSG_OFF));
                hardware_row(&out, TRUE, get_string(MSG_QUEUING), buffer);

                snprintf(buffer, sizeof(buffer), "%d", hw_info.battMemData.scsi_id);
                hardware_row(&out, TRUE, get_string(MSG_SCSI_HOST_ID), buffer);

            }
            else {
                copy_string(buffer, get_string(MSG_NA), sizeof(buffer));
                hardware_row(&out, TRUE, get_string(MSG_NA), NULL);

            }
        }
    }
}

/* Callers choose precision and retain their unmeasured/zero placeholders. */
void format_transfer_rate(ULONG speed, BOOL fractional_kb,
                          char *buffer, size_t size)
{
    if (speed >= 1000000) {
        snprintf(buffer, size, "%lu.%lu MB/s",
                 (unsigned long)(speed / 1000000),
                 (unsigned long)((speed % 1000000) / 100000));
    } else if (speed >= 10000) {
        if (fractional_kb) {
            snprintf(buffer, size, "%lu.%lu KB/s",
                     (unsigned long)(speed / 1000),
                     (unsigned long)((speed % 1000) / 100));
        } else {
            snprintf(buffer, size, "%lu KB/s", (unsigned long)(speed / 1000));
        }
    } else {
        snprintf(buffer, size, "%lu B/s", (unsigned long)speed);
    }
}

ULONG board_detail_count(const BoardInfo *board)
{
    if (!board) return 0;
    return board->board_type == BOARD_ZORRO_II ||
           board->board_type == BOARD_ZORRO_III ?
           BOARD_FIELD_COUNT : BOARD_FIELD_SYSTEM_MEMORY;
}

LocaleStringID board_field_label(BoardField field, BoardType type)
{
    static const LocaleStringID labels[BOARD_FIELD_COUNT] = {
        MSG_BOARD_ADDRESS, MSG_BOARD_SIZE, MSG_BOARD_TYPE,
        MSG_PRODUCT, MSG_MANUFACTURER, MSG_SERIAL_NO,
        MSG_BOARD_SYSTEM_MEMORY, MSG_BOARD_MEMORY_SPACE,
        MSG_BOARD_ROM_VALID, MSG_BOARD_ROM_VECTOR, MSG_BOARD_CHAINED,
        MSG_BOARD_SHUTUP, MSG_BOARD_ZORRO_III, MSG_BOARD_EXTENDED,
        MSG_BOARD_SUBSIZE
    };
    if (field == BOARD_FIELD_SERIAL && type == BOARD_PCI)
        return MSG_BOARD_PCI_CLASS;
    return (unsigned)field < BOARD_FIELD_COUNT ? labels[field] : MSG_NA;
}

const char *format_board_field(const BoardInfo *board, BoardDisplay display,
                              BoardField field, char *buffer, size_t size)
{
    /* Logical Zorro III sizes are encoded in er_Flags, not er_Type.
     * Entries are in 64 KiB units, as in expansion.library's table. */
    static const UWORD sub_sizes[] = {
        0, 0, 1, 2, 4, 8, 16, 32, 64, 96, 128, 160, 192, 224
    };
    ULONG sub_size;
    BOOL enabled;

    if (!board || (unsigned)field >= board_detail_count(board))
        return get_string(MSG_NA);

    switch (field) {
        case BOARD_FIELD_ADDRESS: return board->address_string;
        case BOARD_FIELD_SIZE: return board->size_string;
        case BOARD_FIELD_TYPE: return get_board_type_string(board->board_type);
        case BOARD_FIELD_PRODUCT:
            if (display == BOARD_DISPLAY_NAMES) return board->product_name;
            snprintf(buffer, size, display == BOARD_DISPLAY_HEX ?
                (board->board_type == BOARD_PCI ? "$%04lX" : "$%02lX") : "%lu",
                (unsigned long)board->product_id);
            return buffer;
        case BOARD_FIELD_MANUFACTURER:
            if (display == BOARD_DISPLAY_NAMES) return board->manufacturer_name;
            snprintf(buffer, size, display == BOARD_DISPLAY_HEX ? "$%04lX" : "%lu",
                     (unsigned long)board->manufacturer_id);
            return buffer;
        case BOARD_FIELD_SERIAL:
            if (board->board_type == BOARD_PCI || display == BOARD_DISPLAY_NAMES)
                return board->detail_string;
            snprintf(buffer, size, display == BOARD_DISPLAY_HEX ? "$%08lX" : "%lu",
                     (unsigned long)board->serial_number);
            return buffer;
        case BOARD_FIELD_SYSTEM_MEMORY:
            enabled = board->autoconfig_type & ERTF_MEMLIST;
            break;
        case BOARD_FIELD_MEMORY_SPACE:
            if (board->board_type == BOARD_ZORRO_III)
                return get_string(board->autoconfig_flags & ERFF_MEMSPACE ?
                                  MSG_BOARD_MEMORY_DEVICE : MSG_BOARD_IO_DEVICE);
            return get_string(board->autoconfig_flags & ERFF_MEMSPACE ?
                              MSG_BOARD_8MB_SPACE : MSG_BOARD_ANY_SPACE);
        case BOARD_FIELD_ROM_VALID:
            enabled = board->autoconfig_type & ERTF_DIAGVALID;
            break;
        case BOARD_FIELD_ROM_VECTOR:
            if (!(board->autoconfig_type & ERTF_DIAGVALID))
                return get_string(MSG_NA);
            snprintf(buffer, size, "$%04lX", (unsigned long)board->diagnostic_vector);
            return buffer;
        case BOARD_FIELD_CHAINED:
            enabled = board->autoconfig_type & ERTF_CHAINEDCONFIG;
            break;
        case BOARD_FIELD_SHUTUP:
            enabled = !(board->autoconfig_flags & ERFF_NOSHUTUP);
            break;
        case BOARD_FIELD_ZORRO_III:
            enabled = board->autoconfig_flags & ERFF_ZORRO_III;
            break;
        case BOARD_FIELD_EXTENDED:
            if (board->board_type != BOARD_ZORRO_III) return get_string(MSG_NA);
            enabled = board->autoconfig_flags & ERFF_EXTENDED;
            break;
        case BOARD_FIELD_SUBSIZE:
            if (board->board_type != BOARD_ZORRO_III) return get_string(MSG_NA);
            sub_size = board->autoconfig_flags & ERT_Z3_SSMASK;
            if (sub_size == 0) return get_string(MSG_BOARD_SIZE_MATCHES);
            if (sub_size == 1) return get_string(MSG_BOARD_AUTO_SIZE);
            if (sub_size >= sizeof(sub_sizes) / sizeof(sub_sizes[0]))
                return get_string(MSG_BOARD_RESERVED);
            format_board_size((ULONG)sub_sizes[sub_size] << 16, buffer, size);
            return buffer;
        default:
            return get_string(MSG_NA);
    }
    return get_string(enabled ? MSG_YES : MSG_NO);
}
