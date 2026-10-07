// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2025 Stefan Reinauer

/*
 * xSysInfo - Locale/string handling
 * Default English strings with locale.library catalog support
 */

#include <proto/locale.h>
#include <libraries/locale.h>

#include "xsysinfo.h"
#include "locale_str.h"
#ifdef __KICK13__
#include "tagitem.h"
#endif

/* Locale library and catalog handles */
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *catalog = NULL;

/* Default English strings */
static const char *default_strings[MSG_COUNT] = {
    /* Header strings */
    /* MSG_TAGLINE */                "An Amiga System Information Program",
    /* MSG_CONTACT_LABEL */          "Contact:",

    /* Section headers */
    /* MSG_SYSTEM_SOFTWARE */        "SYSTEM SOFTWARE",
    /* MSG_INTERNAL_HARDWARE */      "HARDWARE",
    /* MSG_SPEED_COMPARISONS */      "SPEED COMPARISONS",
    /* MSG_MEMORY_INFO */            "MEMORY INFORMATION",
    /* MSG_BOARDS_INFO */            "AUTOCONFIG BOARDS INFORMATION",
    /* MSG_DRIVES_INFO */            "DRIVES INFORMATION",
    /* MSG_SCSI_INFO */              "SCSI DEVICE INFORMATION",

    /* Software type cycle */
    /* MSG_LIBRARIES */              "LIBRARIES",
    /* MSG_DEVICES */                "DEVICES",
    /* MSG_RESOURCES */              "RESOURCES",
    /* MSG_MMU_ENTRIES */            "MMU SCAN",

    /* Scale toggle */
    /* MSG_EXPAND */                 "EXPAND",
    /* MSG_SHRINK */                 "SHRINK",

    /* Hardware labels */
    /* MSG_CLOCK */                  "Clock",
    /* MSG_DMA_GFX */                "DMA/Gfx",
    /* MSG_MODE */                   "Mode",
    /* MSG_DISPLAY */                "Display",
    /* MSG_CPU_MHZ */                "CPU/MHz",
    /* MSG_FPU */                    "FPU",
    /* MSG_MMU */                    "MMU",
    /* MSG_VBR */                    "VBR",
    /* MSG_SSP */                    "SSP",
    /* MSG_COMMENT */                "Comment",
    /* MSG_HORIZ_KHZ */              "Horiz kHz",
    /* MSG_ECLOCK_HZ */              "EClock Hz",
    /* MSG_RAM_CONTROLLER */         "RAM ctrl.",
    /* MSG_DECODING */               "Decoding",
    /* MSG_CARD_SLOT */              "Card Slot",
    /* MSG_VERT_HZ */                "Vert Hz",
    /* MSG_SUPPLY_HZ */              "Supply Hz",

    /* Cache labels */
    /* MSG_ICACHE */                 "ICache",
    /* MSG_DCACHE */                 "DCache",
    /* MSG_IBURST */                 "IBurst",
    /* MSG_DBURST */                 "DBurst",
    /* MSG_CBACK */                  "CBack",
    /* MSG_SUPER_SCALAR */           "SuperS/BranchC/StoreB",

    /* Speed comparison labels */
    /* MSG_DHRYSTONES */             "Dhrystones",
    /* MSG_MIPS */                   "MIPS",
    /* MSG_MFLOPS */                 "MFLOPS",
    /* MSG_MEM_SPEED_UNIT */         "MB/s",

    /* Reference system names */
    /* MSG_REF_A600 */               "A600  68000  7MHz",
    /* MSG_REF_B2000 */              "B2000 68000  7MHz",
    /* MSG_REF_A1200 */              "A1200 EC020 14MHz",
    /* MSG_REF_A2500 */              "A2500 68020 14MHz",
    /* MSG_REF_A3000 */              "A3000 68030 25MHz",
    /* MSG_REF_A4000 */              "A4000 68040 25MHz",
    /* MSG_REF_YOU */                "You",

    /* Memory view labels */
    /* MSG_START_ADDRESS */          "Start address",
    /* MSG_END_ADDRESS */            "End address",
    /* MSG_TOTAL_SIZE */             "Total size",
    /* MSG_MEMORY_TYPE */            "Memory type",
    /* MSG_PRIORITY */               "Priority",
    /* MSG_LOWER_BOUND */            "Lower bound",
    /* MSG_UPPER_BOUND */            "Upper bound",
    /* MSG_FIRST_ADDRESS */          "First address",
    /* MSG_AMOUNT_FREE */            "Amount free",
    /* MSG_LARGEST_BLOCK */          "Largest block",
    /* MSG_NUM_CHUNKS */             "Number of chunks",
    /* MSG_NODE_NAME */              "Node name",
    /* MSG_MEMORY_SPEED */           "Memory speed",

    /* Drives view labels */
    /* MSG_DISK_ERRORS */            "Number of disk errors",
    /* MSG_UNIT_NUMBER */            "Unit number",
    /* MSG_DISK_STATE */             "Disk state",
    /* MSG_TOTAL_BLOCKS */           "Total number of blocks",
    /* MSG_BLOCKS_USED */            "Total blocks used",
    /* MSG_BYTES_PER_BLOCK */        "Block size",
    /* MSG_DISK_TYPE */              "Drive/disk type",
    /* MSG_VOLUME_NAME */            "Volume name",
    /* MSG_DEVICE_NAME */            "Device name",
    /* MSG_SURFACES */               "Surfaces",
    /* MSG_SECTORS_PER_SIDE */       "Sectors per side",
    /* MSG_RESERVED_BLOCKS */        "Reserved blocks",
    /* MSG_LOWEST_CYLINDER */        "Lowest cylinder",
    /* MSG_HIGHEST_CYLINDER */       "Highest cylinder",
    /* MSG_NUM_BUFFERS */            "Number of buffers",
    /* MSG_SPEED */                  "Drive speed",
    /* MSG_DRIVES_NO_DRIVES_FOUND */ "No drives found.",
    /* MSG_DASH_PLACEHOLDER */       "---",
    /* MSG_DISK_NO_DISK_INSERTED */  "No Disk Inserted",

    /* Boards view labels */
    /* MSG_BOARD_ADDRESS */          "Board Address",
    /* MSG_BOARD_SIZE */             "Board Size",
    /* MSG_BOARD_TYPE */             "Board Type",
    /* MSG_PRODUCT */                "Product",
    /* MSG_MANUFACTURER */           "Manufacturer",
    /* MSG_SERIAL_NO */              "Serial No.",
    /* MSG_BOARDS_NO_BOARDS_FOUND */ "No expansion boards found",

    /* Button labels */
    /* MSG_BTN_QUIT */               "QUIT",
    /* MSG_BTN_MEMORY */             "MEMORY",
    /* MSG_BTN_DRIVES */             "DRIVES",
    /* MSG_BTN_BOARDS */             "BOARDS",
    /* MSG_BTN_SPEED */              "SPEED",
    /* MSG_BTN_PRINT */              "PRINT",
    /* MSG_BTN_PREV */               "PREV",
    /* MSG_BTN_NEXT */               "NEXT",
    /* MSG_BTN_EXIT */               "EXIT",
    /* MSG_BTN_SCSI */               "SCSI",
    /* MSG_BTN_OK */                 "OK",
    /* MSG_BTN_CANCEL */             "CANCEL",
    /* MSG_BTN_ALL */                "ALL",

    /* Status and values */
    /* MSG_NA */                     "N/A",
    /* MSG_NONE */                   "None",
    /* MSG_UNKNOWN */                "Unknown",
    /* MSG_YES */                    "Yes",
    /* MSG_NO */                     "No",
    /* MSG_ON */                     "On",
    /* MSG_OFF */                    "Off",
    /* MSG_IN_USE */                 "In use",
    /* MSG_CLOCK_FOUND */            "Clock found",
    /* MSG_CLOCK_NOT_FOUND */        "Not found",
    /* MSG_DISK_OK */                "Disk OK, Read/Write",
    /* MSG_DISK_WRITE_PROTECTED */   "Disk OK, Write Protected",
    /* MSG_DISK_NO_DISK */           "No Disk Present",

    /* Hardware modes */
    /* MSG_MODE_PAL */               "PAL",
    /* MSG_MODE_NTSC */              "NTSC",
    /* MSG_SLOT_PCMCIA */            "PCMCIA",

    /* Zorro types */
    /* MSG_ZORRO_II */               "Zorro II",
    /* MSG_ZORRO_III */              "Zorro III",

    /* Memory types */
    /* MSG_CHIP_RAM */               "Chip RAM",
    /* MSG_FAST_RAM */               "Fast RAM",
    /* MSG_SLOW_RAM */               "Slow RAM",
    /* MSG_ROM */                    "ROM",
    /* MSG_24BIT_RAM */              "24-bit RAM",
    /* MSG_32BIT_RAM */              "32-bit RAM",
    /* MSG_MEM_SPEED_HEADER */       "Chip  Fast  ROM",

    /* SCSI Types */
    /* MSG_SCSI_TYPE_DISK */         "Disk",
    /* MSG_SCSI_TYPE_TAPE */         "Tape",
    /* MSG_SCSI_TYPE_PRINTER */      "Printer",
    /* MSG_SCSI_TYPE_PROCESSOR */    "Processor",
    /* MSG_SCSI_TYPE_WORM */         "WORM",
    /* MSG_SCSI_TYPE_CDROM */        "CD",
    /* MSG_SCSI_TYPE_SCANNER */      "Scanner",
    /* MSG_SCSI_TYPE_OPTICAL */      "Optical",
    /* MSG_SCSI_TYPE_CHANGER */      "Changer",
    /* MSG_SCSI_TYPE_COMM */         "Comm",

    /* SCSI Versions */
    /* MSG_SCSI_VER_1 */             "SCSI-1",
    /* MSG_SCSI_VER_2 */             "SCSI-2",
    /* MSG_SCSI_VER_3 */             "SCSI-3",

    /* SCSI View Headers */
    /* MSG_SCSI_ID */                "ID",
    /* MSG_SCSI_TYPE */              "Type",
    /* MSG_SCSI_MANUF */             "Manuf",
    /* MSG_SCSI_MODEL */             "Model",
    /* MSG_SCSI_REV */               "Rev",
    /* MSG_SCSI_MAXBLOCKS */         "MaxBlocks",
    /* MSG_SCSI_ANSI */              "ANSI",
    /* MSG_SCSI_REAL */              "Real",
    /* MSG_SCSI_FORMAT */            "Format",
    /* MSG_SCSI_NO_DEVICES */        "No SCSI devices found",

    /* Filesystem types */
    /* MSG_OFS */                    "Old File System",
    /* MSG_FFS */                    "Fast File System",
    /* MSG_INTL_OFS */               "Intl Old File System",
    /* MSG_INTL_FFS */               "Intl Fast File System",
    /* MSG_DCACHE_OFS */             "DC Old File System",
    /* MSG_DCACHE_FFS */             "DC Fast File System",
    /* MSG_LNFS_OFS */               "LNFS Old File System",
    /* MSG_LNFS_FFS */               "LNFS Fast File System",
    /* MSG_SFS */                    "Smart File System",
    /* MSG_PFS */                    "Professional File System",
    /* MSG_UNKNOWN_FS */             "Unknown File System",

    /* Requester dialogs */
    /* MSG_ENTER_FILENAME */         "Enter Filename or RETURN",
    /* MSG_MEASURING_SPEED */        "Measuring Speed",

    /* Error messages */
    /* MSG_ERR_NO_IDENTIFY */        "Could not open identify.library v13+",
    /* MSG_ERR_NO_MEMORY */          "Out of memory",
    /* MSG_ERR_NO_SCREEN */          "Could not open screen",
    /* MSG_ERR_NO_WINDOW */          "Could not open window",

    /* Comments based on system speed */
    /* MSG_COMMENT_WARP11 */         "WARP 11!",
    /* MSG_COMMENT_LUDICROUS */      "Ludicrous speed!",
    /* MSG_COMMENT_RIDICULOUS */     "Ridiculous speed!",
    /* MSG_COMMENT_BLAZING */        "Blazingly fast!",
    /* MSG_COMMENT_VERY_FAST */      "Very fast!",
    /* MSG_COMMENT_FAST */           "Fast system",
    /* MSG_COMMENT_GOOD */           "Good speed",
    /* MSG_COMMENT_CLASSIC */        "Classic Amiga",
    /* MSG_COMMENT_DEFAULT */        "What can I say!",

    /* Hardware toggle */
    /* MSG_HARDWARE_STD */           "OVERVIEW",
    /* MSG_HARDWARE_CPU */           "CPU",
    /* MSG_HARDWARE_EXT */           "CHIPSET",
    /* MSG_RAMSEY_CTRL */            "Ramsey control",
    /* MSG_SDMAC_REV */              "SCSI chip",
    /* MSG_RAMSEY_PAGE */            "Page mode",
    /* MSG_RAMSEY_BURST */           "Burst",
    /* MSG_RAMSEY_WRAP */            "Wrap",
    /* MSG_RAMSEY_SIZE */            "RAM size",
    /* MSG_RAMSEY_SKIP */            "Skip",
    /* MSG_RAMSEY_REFRESH */         "Refresh",
    /* MSG_1M */                     "1Mx4 chips",
    /* MSG_256K */                   "256Kx4 chips",
    /* MSG_GARY_A1000 */             "A1000",
    /* MSG_GARY_A500 */              "Gary",
    /* MSG_GAYLE */                  "Gayle",
    /* MSG_FAT_GARY */               "Fat Gary",
    /* MSG_GARY_UNKNOWN */           "Unknown",
    [MSG_NV_RAM] =                   "NV-RAM (BattMem):",
    /* MSG_AMNESIA */                "Amnesia",
    /* MSG_SHARED_AMNESIA */         "Shared amn.",
    /* MSG_TIMEOUT */                "Timeout",
    /* MSG_SCAN_LUN */               "Scan LUNs",
    /* MSG_SYNC_TRANS */             "Sync",
    /* MSG_FAST_SYNC */              "Fast sync",
    /* MSG_QUEUING */                "Queuing",
    /* MSG_SCSI_HOST_ID */           "SCSI ID",
    /* MSG_LONG */                   "Long",
    /* MSG_SHORT */                  "Short",
    /* MSG_NCR_53C710 */             "NCR 53C710",
    /* MSG_SDMAC */                  "Super DMAC",
    /* MSG_MSM6242B */               "OKI MSM6242B",
    /* MSG_RP5C01A */                "RICOH RP5C01A",
    /* MSG_MK48T02 */                "ST MK48T02",
    /* MSG_SOUND_SYSTEM */           "Sound",
    /* MSG_PAULA_UNKNOWN */          "Unknown Paula",
    /* MSG_PAULA_ORIG */             "Paula 8364",
    /* MSG_PAULA_SAGA */             "SAGA Arne",
    /* MSG_DENISE_OCS */             "Denise 8362 (OCS)",
    /* MSG_DENISE_ECS */             "Denise 8373 (ECS)",
    /* MSG_DENISE_LISA */            "Lisa 4203",
    /* MSG_DENISE_SAGA */            "SAGA",
    /* MSG_DENISE_UNKNOWN */         "Unknown Denise",
    /* MSG_AGNUS_ALICE_PAL */        "Alice 8374",
    /* MSG_AGNUS_ALICE_NTSC */       "Alice 8374",
    /* MSG_AGNUS_ECS_2MB_PAL */      "Agnus 8375/72B (ECS)",
    /* MSG_AGNUS_ECS_2MB_NTSC */     "Agnus 8375/72B (ECS)",
    /* MSG_AGNUS_ECS_B_PAL */        "Agnus 8372B/75 (ECS)",
    /* MSG_AGNUS_ECS_B_NTSC */       "Agnus 8372B/75 (ECS)",
    /* MSG_AGNUS_ECS_PAL */          "FatAgnus 8372 (ECS)",
    /* MSG_AGNUS_ECS_NTSC */         "FatAgnus 8372 (ECS)",
    /* MSG_AGNUS_OCS_FAT_PAL */      "FatAgnus 8371 (OCS)",
    /* MSG_AGNUS_OCS_FAT_NTSC */     "FatAgnus 8370 (OCS)",
    /* MSG_AGNUS_OCS_PAL */          "Agnus 8367 (OCS)",
    /* MSG_AGNUS_OCS_NTSC */         "Agnus 8361 (OCS)",
    /* MSG_AGNUS_SAGA */             "Agnus SAGA",
    /* MSG_AGNUS_UNKNOWN */          "Unknown Agnus",
    /* MSG_UNCERTAIN */              "uncertain",
    /* MSG_MMU_SIZE */               "MMU page size",
    /* The following strings must be max 48 chars per line. */
    /* MSG_MMU_ADDRESS_HINT */       "Addresses in hex!",
    /* MSG_MMU_FLAGS1_HINT */        "WP=Write Prot. U=Used M=Modified G=Global",
    /* MSG_MMU_FLAGS2_HINT */        "TT=Translated UPx=User-pageX CI=CacheInhibit",
    /* MSG_MMU_FLAGS3_HINT */        "IM=Imprecise NS=non-serial CB=CopyBack",
    /* MSG_MMU_FLAGS4_HINT */        "SO=Supervisor BL=Blank SH=Shared INV=Invalid",
    /* MSG_MMU_FLAGS5_HINT */        "SNG=Single Page RP=Repairable IO=IOspace",
    /* MSG_MMU_FLAGS6_HINT */        "Ux=UserX SW=Swapped MAP=Remapped BN=Bundled",
    /* MSG_MMU_FLAGS7_HINT */        "IND=Indirect +=more flags",
    /* MSG_SLOT_ZORRO */             "Zorro",

    /* MSG_SOFTWARE_OVERVIEW */      "OVERVIEW",
    /* MSG_OPERATING_SYSTEM */       "OS",
    /* MSG_ACTIVE_ROM */             "Active ROM",
    /* MSG_WORKBENCH */              "Workbench",
    /* MSG_SETPATCH */               "SetPatch",
    /* MSG_GRAPHICS_SYSTEM */        "Graphics",
    /* MSG_NATIVE_GRAPHICS */        "AmigaOS (native)",
    /* MSG_BOARD_NAMES */            "NAMES",
    /* MSG_BOARD_DECIMAL */          "DEC",
    /* MSG_BOARD_HEX */              "HEX",
    /* MSG_UNKNOWN_OS */             "Unknown OS",
    /* MSG_BUS_MHZ */                "Bus MHz",
    /* MSG_HARDWARE_CLOCK */          "CLOCK",
    /* MSG_RTC_DATE */                "RTC date",
    /* MSG_RTC_TIME */                "RTC time",
    /* MSG_NCR_53C770 */              "NCR 53C770",

    /* Cache toggle button labels */
    /* MSG_BTN_ON */                 "ON",
    /* MSG_BTN_OFF */                "OFF",
    /* MSG_1MX1 */                   "1Mx1 chips",
    /* MSG_DMA_CHIP */               "DMA chip",
    /* MSG_DMA_VERSION */            "Firmware",
    /* MSG_RESDMAC */                "ReSDMAC",
    /* MSG_WD33C93_FAMILY */         "WD33C93 family",
    /* MSG_WD33C93 */                 "WD33C93",
    /* MSG_WD33C93A */                "WD33C93A",
    /* MSG_WD33C93B */                "WD33C93B",
    /* MSG_WD_MICROCODE */            "Microcode",
    /* MSG_WD_CLOCK */                "SCSI clock",
    /* MSG_WD_MODE */                 "I/O mode",
    /* MSG_WD_SYNC_OFFSET */          "Sync / offset",
    /* MSG_WD_POLLED */               "Polled",
    /* MSG_WD_BUS */                  "WD bus",
    /* MSG_WD_ASYNC */                "Async",
    /* MSG_WD_BUSY */                 "SCSI controller busy; check skipped.",
    /* MSG_WD_UNAVAILABLE */          "WD check unavailable for this configuration.",
    /* MSG_WD_CLOCK_FAILED */         "WD clock could not be measured.",
    /* MSG_WD_RESTORE_FAILED */       "WD restore failed. Reboot before disk access.",
    /* MSG_WD_FAILED */               "WD check failed; controller restored.",
    /* MSG_DMA_INTEGRATED */        "Integrated",
    /* MSG_NCR_DMA_BURST */         "DMA burst",
    /* MSG_NCR_TRANSFERS */         "%u transfers",
    /* MSG_NCR_WIDTH */             "Data width",
    /* MSG_NCR_PARITY */            "Parity check",
    /* MSG_NCR_DOUBLER */           "Clock doubler",
    /* MSG_CHIP_ID */ "Chip ID",
    /* MSG_PCMCIA_CARD */ "PCMCIA card",
    /* MSG_PCMCIA_SLOT */ "PCMCIA slot",
    /* MSG_PCMCIA_ACCESS */ "Access time",
    /* MSG_AKIKO_C2P */ "C2P (OS)",
    /* MSG_PPC_REVISION */ "PPC revision",
    /* MSG_PPC_BUS */ "PPC bus MHz",
    /* MSG_PPC_RUNTIME */ "PPC runtime",
    /* MSG_HARDWARE_SCSI */ "SCSI",
    /* General titles, actions and list headings */
    /* MSG_SOFTWARE_TITLE */ "Software",
    /* MSG_HARDWARE_TITLE */ "Hardware",
    /* MSG_CHIPSET_TITLE */ "Chipset",
    /* MSG_SPEED_TITLE */ "Speed",
    /* MSG_MEMORY_TITLE */ "Memory",
    /* MSG_DRIVES_TITLE */ "Drives",
    /* MSG_BOARDS_TITLE */ "Boards",
    /* MSG_PROJECT_MENU */ "Project",
    /* MSG_ABOUT */ "About...",
    /* MSG_ABOUT_MUI */ "About MUI...",
    /* MSG_RUN_BENCHMARKS */ "Run benchmarks",
    /* MSG_SAVE_REPORT */ "Save report...",
    /* MSG_QUIT */ "Quit",
    /* MSG_SETTINGS_MENU */ "Settings",
    /* MSG_MUI_SETTINGS */ "MUI...",
    /* MSG_MEASURE_SPEED */ "Measure speed",
    /* MSG_SCSI_DEVICES */ "SCSI devices...",
    /* MSG_CLOSE */ "Close",
    /* MSG_RESULTS_TITLE */ "Results",
    /* MSG_NAME */ "Name",
    /* MSG_LOCATION */ "Location",
    /* MSG_ADDRESS */ "Address",
    /* MSG_VERSION */ "Version",
    /* MSG_SIZE */ "Size",
    /* MSG_DRIVE */ "Drive",
    /* MSG_VOLUME */ "Volume",
    /* MSG_REPORT_SAVED */ "Report saved to %s",
    /* MSG_REPORT_SAVE_FAILED */ "Could not save report",
    /* MSG_FONT_INVALID */ "Invalid FONT setting. Use FONT=name.size.",
    /* MSG_FONT_UNAVAILABLE */ "Could not open the requested font.",
    /* MSG_FONT_TOO_LARGE */ "Font exceeds the 8 x 8 pixel limit.",
    /* MSG_FONT_FALLBACK */ "Using topaz.8.",
    /* MSG_BOARD_DETAILS */ "Details",
    /* MSG_BOARD_DETAILS_TITLE */ "Expansion board details",
    /* MSG_BOARD_BACK */ "Back",
    /* MSG_BOARD_PCI_CLASS */ "PCI class",
    /* MSG_BOARD_SYSTEM_MEMORY */ "System memory",
    /* MSG_BOARD_MEMORY_SPACE */ "Memory / I/O space",
    /* MSG_BOARD_ROM_VALID */ "ROM vector valid",
    /* MSG_BOARD_ROM_VECTOR */ "ROM vector offset",
    /* MSG_BOARD_CHAINED */ "Next board related",
    /* MSG_BOARD_SHUTUP */ "Can shut up",
    /* MSG_BOARD_ZORRO_III */ "Zorro III flag",
    /* MSG_BOARD_EXTENDED */ "Extended size (Z3)",
    /* MSG_BOARD_SUBSIZE */ "Logical size (Z3)",
    /* MSG_BOARD_SIZE_MATCHES */ "Matches physical size",
    /* MSG_BOARD_AUTO_SIZE */ "Automatic size",
    /* MSG_BOARD_RESERVED */ "Reserved",
    /* MSG_BOARD_MEMORY_DEVICE */ "Memory device",
    /* MSG_BOARD_IO_DEVICE */ "I/O device",
    /* MSG_BOARD_8MB_SPACE */ "8 MB expansion space",
    /* MSG_BOARD_ANY_SPACE */ "Any address space",
    /* MSG_ECLOCK */          "EClock",
    /* MSG_REPORT_OPEN */ "Report...",
    /* MSG_REPORT_TITLE */ "System report",
    /* MSG_REPORT_WHICH */ "WhichAmiga",
    /* MSG_REPORT_BRIEF */ "Brief",
    /* MSG_REPORT_FULL */ "Full",
    /* MSG_REPORT_SAVE_AS */ "Save as...",
    /* MSG_REPORT_FAILED */ "Could not create report preview",
    /* MSG_BTN_REPORT */ "REPORT"
};

/* Get string by ID - uses catalog if available, falls back to English */
const char *get_string(LocaleStringID id)
{
    if (id >= 0 && id < MSG_COUNT) {
        if (catalog) {
            return (const char *)GetCatalogStr(catalog, id,
                       (CONST_STRPTR)default_strings[id]);
        }
        return default_strings[id];
    }
    return "???";
}

/* Initialize locale - opens locale.library and catalog if available */
BOOL init_locale(void)
{
    /* Try to open locale.library (available from Workbench 2.1+) */
    LocaleBase = (struct LocaleBase *)OpenLibrary((CONST_STRPTR)"locale.library", 38);
    if (LocaleBase) {
        /* Open catalog - locale.library will find the appropriate translation
         * based on user's Locale preferences. Catalog is expected in:
         * LOCALE:Catalogs/<language>/xSysInfo.catalog
         * or PROGDIR:Catalogs/<language>/xSysInfo.catalog
         */
        catalog = OpenCatalog(NULL, (CONST_STRPTR)"xSysInfo.catalog",
                              OC_BuiltInLanguage, (ULONG)"english",
                              TAG_DONE);
        /* catalog may be NULL if no translation available - that's OK,
         * we'll use the built-in English strings */
    }
    /* Always return TRUE - locale support is optional */
    return TRUE;
}

/* Cleanup locale */
void cleanup_locale(void)
{
    if (catalog) {
        CloseCatalog(catalog);
        catalog = NULL;
    }
    if (LocaleBase) {
        CloseLibrary((struct Library *)LocaleBase);
        LocaleBase = NULL;
    }
}
