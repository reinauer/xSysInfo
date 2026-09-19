// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#include <stdio.h>
#include <string.h>

#include <libraries/identify.h>

#include <proto/dos.h>

#include "which.h"

typedef struct {
    BPTR fh;
    BOOL failed;
} WhichOutput;

static void output_line(void *context, const char *line)
{
    WhichOutput *output = (WhichOutput *)context;
    LONG length = (LONG)strlen(line);

    if ((length && Write(output->fh, line, length) != length) ||
        Write(output->fh, "\n", 1) != 1) {
        output->failed = TRUE;
    }
}

static void format_mhz(char *buffer, ULONG size, ULONG mhz_x100)
{
    ULONG tenths = (mhz_x100 + 5) / 10;

    snprintf(buffer, size, "%lu.%lu",
             (unsigned long)(tenths / 10),
             (unsigned long)(tenths % 10));
}

static const char *kickstart_name(UWORD version, UWORD revision)
{
    switch (version) {
    case 31:
        return "Kickstart 1.1";
    case 33:
        return "Kickstart 1.2";
    case 34:
        return "Kickstart 1.3";
    case 36:
        return "Kickstart 2.0";
    case 37:
        return revision >= 299 ? "Kickstart 2.05" : "Kickstart 2.04";
    case 38:
        return "Kickstart 2.1";
    case 39:
        return "Kickstart 3.0";
    case 40:
        return "Kickstart 3.1";
    case 43:
        return "Kickstart 3.2 prototype";
    case 44:
        return "Kickstart 3.5";
    case 45:
        return "Kickstart 3.9";
    case 46:
        return "Kickstart 3.1.4";
    case 47:
        if (revision >= 115)
            return "Kickstart 3.2.3";
        if (revision >= 111)
            return "Kickstart 3.2.2";
        if (revision >= 102)
            return "Kickstart 3.2.1";
        return "Kickstart 3.2";
    case 48:
        return "Kickstart 3.3";
    default:
        return "Kickstart unknown";
    }
}

static const char *workbench_name(ULONG os_number, UWORD version, UWORD revision,
                                  UWORD active_version,
                                  UWORD active_revision)
{
    /*
     * IDHW_OSNR combines exec/ROM and version.library revisions, which is
     * necessary because version.library alone cannot distinguish every 3.2
     * point release.
     */
    switch (os_number) {
    case IDOS_2_0:   return "Workbench 2.0";
    case IDOS_2_04:  return "Workbench 2.04";
    case IDOS_2_05:  return "Workbench 2.05";
    case IDOS_2_1:   return "Workbench 2.1";
    case IDOS_3_0:   return "Workbench 3.0";
    case IDOS_3_1:   return "Workbench 3.1";
    case IDOS_3_1_4: return "Workbench 3.1.4";
    case IDOS_3_5:
    case IDOS_3_5_BB1:
    case IDOS_3_5_BB2:
        return "Workbench 3.5";
    case IDOS_3_9:
    case IDOS_3_9_BB1:
    case IDOS_3_9_BB2:
        return "Workbench 3.9";
    case IDOS_3_2_PROTO:
        return "Workbench 3.2 prototype";
    case IDOS_3_2:   return "Workbench 3.2";
    case IDOS_3_2_1: return "Workbench 3.2.1";
    case IDOS_3_2_2: return "Workbench 3.2.2";
    case IDOS_3_2_3: return "Workbench 3.2.3";
    default:
        break;
    }

    switch (version) {
    case 31:
        return "Workbench 1.1";
    case 33:
        return "Workbench 1.2";
    case 34:
        return "Workbench 1.3";
    case 36:
        return "Workbench 2.0";
    case 37:
        return revision >= 299 ? "Workbench 2.05" : "Workbench 2.04";
    case 38:
        return "Workbench 2.1";
    case 39:
        return "Workbench 3.0";
    case 40:
        return "Workbench 3.1";
    case 43:
        return "Workbench 3.2 prototype";
    case 44:
        return "Workbench 3.5";
    case 45:
        return "Workbench 3.9";
    case 46:
        return "Workbench 3.1.4";
    case 47:
        if (active_version == 47 && active_revision >= 115)
            return "Workbench 3.2.3";
        if ((active_version == 47 && active_revision >= 111) ||
            revision >= 4)
            return "Workbench 3.2.2";
        if ((active_version == 47 && active_revision >= 102) ||
            revision >= 3)
            return "Workbench 3.2.1";
        return "Workbench 3.2";
    case 48:
        return "Workbench 3.3";
    default:
        return "Workbench unknown";
    }
}

static void build_cpu_string(char *buffer, ULONG size,
                             const HardwareInfo *hardware)
{
    const char *name;
    char mhz[24];

    switch (hardware->cpu_type) {
    case CPU_68000:   name = "MC68000"; break;
    case CPU_68010:   name = "MC68010"; break;
    case CPU_68020:   name = "MC68020"; break;
    case CPU_68EC020: name = "MC68EC020"; break;
    case CPU_68030:   name = "MC68030"; break;
    case CPU_68EC030: name = "MC68EC030"; break;
    case CPU_68040:   name = "MC68040"; break;
    case CPU_68LC040: name = "MC68LC040"; break;
    case CPU_68EC040: name = "MC68EC040"; break;
    case CPU_68060:   name = "MC68060"; break;
    case CPU_68EC060: name = "MC68EC060"; break;
    case CPU_68LC060: name = "MC68LC060"; break;
    case CPU_68080:   name = "MC68080"; break;
    case CPU_EMU:     name = "Emu68"; break;
    default:          name = "unknown"; break;
    }

    if (hardware->cpu_mhz) {
        char value[16];
        format_mhz(value, sizeof(value), hardware->cpu_mhz);
        snprintf(mhz, sizeof(mhz), " %s MHz", value);
    } else {
        mhz[0] = '\0';
    }
    if (hardware->cpu_type == CPU_68060 ||
        hardware->cpu_type == CPU_68EC060 ||
        hardware->cpu_type == CPU_68LC060 ||
        hardware->cpu_type == CPU_68080) {
        snprintf(buffer, size, "%s%s (rev %u)", name, mhz,
                 hardware->cpu_rev);
    } else {
        snprintf(buffer, size, "%s%s", name, mhz);
    }
}

static void build_fpu_string(char *buffer, ULONG size,
                             const HardwareInfo *hardware)
{
    const char *name;
    char mhz[24];

    switch (hardware->fpu_type) {
    case FPU_NONE:
        copy_string(buffer, "not available", size);
        return;
    case FPU_68881: name = "MC68881"; break;
    case FPU_68882: name = "MC68882"; break;
    case FPU_68040: name = "68040fpu"; break;
    case FPU_68060: name = "68060fpu"; break;
    case FPU_68080: name = "68080fpu"; break;
    default:
        copy_string(buffer, "not available", size);
        return;
    }

    if (hardware->fpu_mhz) {
        format_mhz(mhz, sizeof(mhz), hardware->fpu_mhz);
        snprintf(buffer, size, "%s %s MHz", name, mhz);
    } else {
        copy_string(buffer, name, size);
    }
}

static void build_mmu_string(char *buffer, ULONG size,
                             const HardwareInfo *hardware)
{
    const char *name;

    switch (hardware->mmu_type) {
    case MMU_68851: name = "68851"; break;
    case MMU_68030: name = "68030"; break;
    case MMU_68040: name = "68040"; break;
    case MMU_68060: name = "68060"; break;
    case MMU_68080: name = "68080"; break;
    default:
        copy_string(buffer, "not available", size);
        return;
    }

    snprintf(buffer, size, "%smmu %s", name,
             hardware->mmu_enabled ? "running" : "not active");
}

static void build_graphics_chip(char *buffer, ULONG size,
                                const HardwareInfo *hardware)
{
    const char *name;

    switch (hardware->denise_type) {
    case DENISE_OCS:    name = "OCS Denise 8362"; break;
    case DENISE_ECS:    name = "ECS Denise 8373"; break;
    case DENISE_LISA:   name = "AGA Lisa 4203"; break;
    case DENISE_ISABEL: name = "SAGA Isabel"; break;
    case DENISE_MONICA: name = "SAGA Monica"; break;
    default:            name = "Unknown"; break;
    }

    snprintf(buffer, size, "%s (rev %u)", name,
             (unsigned int)(hardware->denise_rev & 7));
}

static void build_animation_chip(char *buffer, ULONG size,
                                 const HardwareInfo *hardware)
{
    switch (hardware->agnus_type) {
    case AGNUS_OCS_NTSC:
        copy_string(buffer, "OCS NTSC Agnus 8361 512K", size);
        break;
    case AGNUS_OCS_PAL:
        copy_string(buffer, "OCS PAL Agnus 8367 512K", size);
        break;
    case AGNUS_OCS_FAT_NTSC:
        copy_string(buffer, "OCS NTSC Fat Agnus 8370 512K", size);
        break;
    case AGNUS_OCS_FAT_PAL:
        copy_string(buffer, "OCS PAL Fat Agnus 8371 512K", size);
        break;
    case AGNUS_ECS_NTSC:
        copy_string(buffer, "ECS NTSC Fatter Agnus 8372a 1M", size);
        break;
    case AGNUS_ECS_PAL:
        copy_string(buffer, "ECS PAL Fatter Agnus 8372a 1M", size);
        break;
    case AGNUS_ECS_2MB_NTSC:
        copy_string(buffer, "ECS NTSC Super Agnus 8372b/8375 2M", size);
        break;
    case AGNUS_ECS_2MB_PAL:
        copy_string(buffer, "ECS PAL Super Agnus 8372b/8375 2M", size);
        break;
    case AGNUS_ECS_B_NTSC:
        copy_string(buffer, "ECS NTSC Super Agnus 8372b 2M", size);
        break;
    case AGNUS_ECS_B_PAL:
        copy_string(buffer, "ECS PAL Super Agnus 8372b 2M", size);
        break;
    case AGNUS_ALICE_NTSC:
        snprintf(buffer, size, "AGA NTSC Alice 8374, rev %s",
                 (hardware->agnus_rev & 0x0f) <= 2 ? "0-2" : "3-4");
        break;
    case AGNUS_ALICE_PAL:
        snprintf(buffer, size, "AGA PAL Alice 8374, rev %s",
                 (hardware->agnus_rev & 0x0f) <= 2 ? "0-2" : "3-4");
        break;
    case AGNUS_SAGA:
        copy_string(buffer, "SAGA", size);
        break;
    default:
        copy_string(buffer, "Unknown", size);
        break;
    }
}

static void append_other_chip(char *buffer, ULONG size, const char *chip)
{
    ULONG used = (ULONG)strlen(buffer);

    if (used && used < size - 1) {
        snprintf(buffer + used, size - used, ", %s", chip);
    } else if (!used) {
        copy_string(buffer, chip, size);
    }
}

static void build_other_chips(char *buffer, ULONG size,
                              const HardwareInfo *hardware)
{
    char chip[48];

    buffer[0] = '\0';
    if (hardware->paula_type == PAULA_ORIG) {
        snprintf(chip, sizeof(chip), "Paula 8364 (rev %u)",
                 hardware->paula_rev);
    } else if (hardware->paula_type == PAULA_SAGA) {
        snprintf(chip, sizeof(chip), "SAGA Arne (rev %u)",
                 hardware->paula_rev);
    } else {
        snprintf(chip, sizeof(chip), "Unknown Paula (rev %u)",
                 hardware->paula_rev);
    }
    append_other_chip(buffer, size, chip);

    if (hardware->ramsey_rev) {
        snprintf(chip, sizeof(chip), "Ramsey (rev %u)",
                 hardware->ramsey_rev);
        append_other_chip(buffer, size, chip);
    }

    switch (hardware->gary_type) {
    case GAYLE:
        snprintf(chip, sizeof(chip), "Gayle (rev %u)", hardware->gary_rev);
        append_other_chip(buffer, size, chip);
        break;
    case FAT_GARY:
    case GARY_A500:
        snprintf(chip, sizeof(chip), "Gary (rev %u)", hardware->gary_rev);
        append_other_chip(buffer, size, chip);
        break;
    default:
        break;
    }
}

static void build_graphics_system(char *buffer, ULONG size,
                                  const HardwareInfo *hardware,
                                  const SystemSoftwareInfo *software)
{
    if (software->graphics_system_id != IDGOS_AMIGAOS &&
        software->graphics_system[0]) {
        copy_string(buffer, software->graphics_system, size);
    } else if (hardware->native_graphics == NATIVE_GRAPHICS_AGA) {
        copy_string(buffer, "Amiga AGA display bandwidth: 4x", size);
    } else if (hardware->native_graphics == NATIVE_GRAPHICS_ECS) {
        copy_string(buffer, "Amiga ECS", size);
    } else {
        copy_string(buffer, "Amiga OCS", size);
    }
}

static void build_model_phrase(char *buffer, ULONG size,
                                const HardwareInfo *hardware)
{
    const char *phrase = NULL;

    switch (hardware->amiga_model_id) {
    case IDSYS_AMIGA1000:  phrase = "is an Amiga 1000"; break;
    case IDSYS_AMIGA500:   phrase = "is an Amiga 500"; break;
    case IDSYS_AMIGA2000:  phrase = "is an Amiga 2000"; break;
    case IDSYS_AMIGA3000:  phrase = "is an Amiga 3000"; break;
    case IDSYS_CDTV:       phrase = "is an Amiga CDTV"; break;
    case IDSYS_AMIGA600:   phrase = "is an Amiga 600"; break;
    case IDSYS_CD32:       phrase = "is an Amiga CD"; break;
    case IDSYS_AMIGA1200:  phrase = "is an Amiga 1200"; break;
    case IDSYS_AMIGA4000:  phrase = "is an Amiga 4000"; break;
    case IDSYS_AMIGA4000T: phrase = "is an Amiga 4000 Tower"; break;
    case IDSYS_DRACO:
        phrase = "is MacroSystems' DraCo (Amiga-clone)";
        break;
    case IDSYS_AAA:
    case IDSYS_AAA_DUAL:
        phrase = "probably is a AAA prototype (?)";
        break;
    default:
        break;
    }

    if (phrase) {
        copy_string(buffer, phrase, size);
    } else if (strncmp(hardware->amiga_model_string, "Amiga ", 6) == 0) {
        snprintf(buffer, size, "is an %s",
                 hardware->amiga_model_string);
    } else {
        copy_string(buffer, "is some Amiga or compatible", size);
    }
}

static void emit_version_line(WhichCompatEmitLine emit_line, void *context,
                              const char *label, unsigned int version,
                              unsigned int revision, const char *name)
{
    char line[160];

    snprintf(line, sizeof(line), "%s%u.%u (%s)", label, version, revision,
             name);
    emit_line(context, line);
}

void which_compat_emit(const HardwareInfo *hardware,
                       const SystemSoftwareInfo *versions,
                       const MemoryRegionList *memory, const BoardList *boards,
                       WhichCompatEmitLine emit_line, void *context)
{
    char line[256], value[160];
    ULONG i;
    BOOL have_boards = FALSE;

    if (!hardware || !versions || !memory || !boards || !emit_line)
        return;

    snprintf(line, sizeof(line), "xSysInfo (WhichAmiga Edition) %s (%s)",
             XSYSINFO_VERSION, XSYSINFO_DATE);
    emit_line(context, line);
    emit_line(context,
              "Written by Stefan Reinauer. Copyright \251 2025-2026 Stefan Reinauer.");
    emit_line(context, "");
    emit_line(context, "Evaluating system...");

    build_cpu_string(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), "Central Processing Unit: %s", value);
    emit_line(context, line);
    if (hardware->ramsey_rev) {
        if (hardware->bus_mhz) {
            format_mhz(value, sizeof(value), hardware->bus_mhz);
            snprintf(line, sizeof(line), "        Motherboard bus: %s MHz", value);
            emit_line(context, line);
        } else {
            emit_line(context, "        Motherboard bus: not available");
        }
    }
    build_fpu_string(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), "    Floating Point Unit: %s", value);
    emit_line(context, line);
    build_mmu_string(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), " Memory Management Unit: %s", value);
    emit_line(context, line);
    build_graphics_chip(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), "   Custom graphics chip: %s", value);
    emit_line(context, line);
    build_animation_chip(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), "  Custom animation chip: %s", value);
    emit_line(context, line);
    build_other_chips(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), "   Other custom chip(s): %s", value);
    emit_line(context, line);
    build_graphics_system(value, sizeof(value), hardware, versions);
    snprintf(line, sizeof(line), "        Graphics system: %s", value);
    emit_line(context, line);
    snprintf(line, sizeof(line), "         Hardware Clock: %s",
             hardware->clock_type == CLOCK_NONE ? "not available" : "clock found");
    emit_line(context, line);

    snprintf(line, sizeof(line), " Max. Chipmem available: %lu K",
             (unsigned long)(memory->total_chip_size / 1024));
    emit_line(context, line);
    snprintf(line, sizeof(line), " Max. Fastmem available: %lu K",
             (unsigned long)(memory->total_fast_size / 1024));
    emit_line(context, line);

    emit_version_line(emit_line, context, "       ROM chip version: ",
                      hardware->kickstart_version, hardware->kickstart_revision,
                      kickstart_name(hardware->kickstart_version,
                                    hardware->kickstart_revision));
    if (hardware->kickstart_patch_version != hardware->kickstart_version ||
        hardware->kickstart_patch_revision != hardware->kickstart_revision) {
        emit_version_line(emit_line, context, "  ReKicked ROM, version: ",
                          hardware->kickstart_patch_version,
                          hardware->kickstart_patch_revision,
                          kickstart_name(hardware->kickstart_patch_version,
                                        hardware->kickstart_patch_revision));
    }

    if (versions->has_workbench_version) {
        emit_version_line(emit_line, context, "      Workbench version: ",
                          versions->workbench_version, versions->workbench_revision,
                          workbench_name(versions->os_id, versions->workbench_version,
                                         versions->workbench_revision,
                                         hardware->kickstart_patch_version,
                                         hardware->kickstart_patch_revision));
    } else {
        emit_line(context,
                  "      Workbench version: version information not available");
    }

    if (versions->has_setpatch_version) {
        if (versions->is_tinysetpatch)
            snprintf(line, sizeof(line),
                     "       SetPatch version: %u.%u (TinySetPatch %u.%u)",
                     versions->setpatch_version, versions->setpatch_revision,
                     versions->tinysetpatch_version, versions->tinysetpatch_revision);
        else
            snprintf(line, sizeof(line), "       SetPatch version: %u.%u",
                     versions->setpatch_version, versions->setpatch_revision);
        emit_line(context, line);
    } else {
        emit_line(context,
                  "       SetPatch version: version information not available");
    }

    for (i = 0; i < boards->count; i++) {
        const BoardInfo *board = &boards->boards[i];

        /* WhichAmiga 1.3.3 reports AutoConfig boards, not PCI devices. */
        if (board->board_type == BOARD_PCI)
            continue;
        if (!have_boards) {
            emit_line(context, "     Expansion board(s):");
            have_boards = TRUE;
        }

        if (board->board_size >= 1024 * 1024 &&
            board->board_size % (1024 * 1024) == 0) {
            snprintf(value, sizeof(value), "%luM",
                     (unsigned long)(board->board_size / (1024 * 1024)));
        } else if (board->board_size >= 1024) {
            snprintf(value, sizeof(value), "%luk",
                     (unsigned long)(board->board_size / 1024));
        } else {
            snprintf(value, sizeof(value), "%lu", (unsigned long)board->board_size);
        }
        snprintf(line, sizeof(line), "%u/%u: %s %s (@$%08lX %s)",
                 board->manufacturer_id, board->product_id,
                 board->manufacturer_name, board->product_name,
                 (unsigned long)board->board_address, value);
        emit_line(context, line);
    }

    emit_line(context, "");
    build_model_phrase(value, sizeof(value), hardware);
    snprintf(line, sizeof(line), " Your computer %s.", value);
    emit_line(context, line);
}

BOOL export_which_compatible(BPTR fh)
{
    WhichOutput output;

    if (!fh)
        return FALSE;

    output.fh = fh;
    output.failed = FALSE;
    which_compat_emit(&hw_info, &system_software, &memory_regions, &board_list,
                      output_line, &output);
    return !output.failed;
}
