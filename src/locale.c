/*
 * xSysInfo - Locale/string handling
 * Default English strings, with support for future locale.library integration
 */

#include "xsysinfo.h"
#include "locale_str.h"

/* Default English strings */
static const char *default_strings[MSG_COUNT] = {
    /* MSG_TAGLINE */           "An Amiga System Information Program",
    /* MSG_CONTACT_LABEL */     "Contact:",

    /* Section headers */
    /* MSG_SYSTEM_SOFTWARE */   "SYSTEM SOFTWARE INSTALLED",
    /* MSG_INTERNAL_HARDWARE */ "INTERNAL HARDWARE MODES",
    /* MSG_SPEED_COMPARISONS */ "SPEED COMPARISONS",
    /* MSG_MEMORY_INFO */       "MEMORY INFORMATION",
    /* MSG_BOARDS_INFO */       "AUTOCONFIG BOARDS INFORMATION",
    /* MSG_DRIVES_INFO */       "DRIVES INFORMATION",
    /* MSG_SCSI_INFO */         "SCSI DEVICE INFORMATION",

    /* Software type cycle */
    /* MSG_LIBRARIES */         "LIBRARIES",
    /* MSG_DEVICES */           "DEVICES",
    /* MSG_RESOURCES */         "RESOURCES",

    /* Scale toggle */
    /* MSG_EXPAND */            "EXPAND",
    /* MSG_SHRINK */            "SHRINK",

    /* Hardware labels */
    /* MSG_CLOCK */             "Clock",
    /* MSG_DMA_GFX */           "DMA/Gfx",
    /* MSG_MODE */              "Mode",
    /* MSG_DISPLAY */           "Display",
    /* MSG_CPU_MHZ */           "CPU/MHz",
    /* MSG_FPU */               "FPU",
    /* MSG_MMU */               "MMU",
    /* MSG_VBR */               "VBR",
    /* MSG_COMMENT */           "Comment",
    /* MSG_HORIZ_KHZ */         "Horiz KHz",
    /* MSG_ECLOCK_HZ */         "EClock Hz",
    /* MSG_RAMSEY_REV */        "Ramsey rev",
    /* MSG_GARY_REV */          "Gary rev",
    /* MSG_CARD_SLOT */         "Card Slot",
    /* MSG_VERT_HZ */           "Vert Hz",
    /* MSG_SUPPLY_HZ */         "Supply Hz",

    /* Cache labels */
    /* MSG_ICACHE */            "ICache",
    /* MSG_DCACHE */            "DCache",
    /* MSG_IBURST */            "IBurst",
    /* MSG_DBURST */            "DBurst",
    /* MSG_CBACK */             "CBack",

    /* Speed comparison labels */
    /* MSG_DHRYSTONES */        "Dhrystones",
    /* MSG_MIPS */              "Mips",
    /* MSG_MFLOPS */            "MFlops",
    /* MSG_MEM_SPEED_UNIT */    "MB/s",

    /* Reference system names */
    /* MSG_REF_A600 */          "A600  68000  7MHz",
    /* MSG_REF_B2000 */         "B2000 68000  7MHz",
    /* MSG_REF_A1200 */         "A1200 EC020 14MHz",
    /* MSG_REF_A2500 */         "A2500 68020 14MHz",
    /* MSG_REF_A3000 */         "A3000 68030 25MHz",
    /* MSG_REF_A4000 */         "A4000 68040 25MHz",
    /* MSG_REF_YOU */           "You",

    /* Memory view labels */
    /* MSG_START_ADDRESS */     "START ADDRESS",
    /* MSG_END_ADDRESS */       "END ADDRESS",
    /* MSG_TOTAL_SIZE */        "TOTAL SIZE",
    /* MSG_MEMORY_TYPE */       "MEMORY TYPE",
    /* MSG_PRIORITY */          "PRIORITY",
    /* MSG_LOWER_BOUND */       "LOWER BOUND",
    /* MSG_UPPER_BOUND */       "UPPER BOUND",
    /* MSG_FIRST_ADDRESS */     "FIRST ADDRESS",
    /* MSG_AMOUNT_FREE */       "AMOUNT FREE",
    /* MSG_LARGEST_BLOCK */     "LARGEST BLOCK",
    /* MSG_NUM_CHUNKS */        "NUMBER OF CHUNKS",
    /* MSG_NODE_NAME */         "NODE NAME",
    /* MSG_MEMORY_SPEED */      "MEMORY SPEED",

    /* Drives view labels */
    /* MSG_DISK_ERRORS */       "NUMBER OF DISK ERRORS",
    /* MSG_UNIT_NUMBER */       "UNIT NUMBER",
    /* MSG_DISK_STATE */        "DISK STATE",
    /* MSG_TOTAL_BLOCKS */      "TOTAL NUMBER OF BLOCKS",
    /* MSG_BLOCKS_USED */       "TOTAL BLOCKS USED",
    /* MSG_BYTES_PER_BLOCK */   "BYTES PER BLOCK",
    /* MSG_DISK_TYPE */         "DRIVE/DISK TYPE",
    /* MSG_VOLUME_NAME */       "VOLUME NAME",
    /* MSG_DEVICE_NAME */       "DEVICE NAME",
    /* MSG_SURFACES */          "SURFACES",
    /* MSG_SECTORS_PER_SIDE */  "SECTORS PER SIDE",
    /* MSG_RESERVED_BLOCKS */   "RESERVED BLOCKS",
    /* MSG_LOWEST_CYLINDER */   "LOWEST CYLINDER",
    /* MSG_HIGHEST_CYLINDER */  "HIGHEST CYLINDER",
    /* MSG_NUM_BUFFERS */       "NUMBER OF BUFFERS",
    /* MSG_SPEED */             "DRIVE SPEED",

    /* Boards view labels */
    /* MSG_BOARD_ADDRESS */     "Board Address",
    /* MSG_BOARD_SIZE */        "Board Size",
    /* MSG_BOARD_TYPE */        "Board Type",
    /* MSG_PRODUCT */           "Product",
    /* MSG_MANUFACTURER */      "Manufacturer",
    /* MSG_SERIAL_NO */         "Serial No.",

    /* Button labels */
    /* MSG_BTN_QUIT */          "QUIT",
    /* MSG_BTN_MEMORY */        "MEMORY",
    /* MSG_BTN_DRIVES */        "DRIVES",
    /* MSG_BTN_BOARDS */        "BOARDS",
    /* MSG_BTN_SPEED */         "SPEED",
    /* MSG_BTN_PRINT */         "PRINT",
    /* MSG_BTN_PREV */          "PREV",
    /* MSG_BTN_NEXT */          "NEXT",
    /* MSG_BTN_EXIT */          "EXIT",
    /* MSG_BTN_SCSI */          "SCSI",

    /* Status and values */
    /* MSG_NA */                "N/A",
    /* MSG_NONE */              "NONE",
    /* MSG_YES */               "YES",
    /* MSG_NO */                "NO",
    /* MSG_IN_USE */            "IN USE",
    /* MSG_CLOCK_FOUND */       "CLOCK FOUND",
    /* MSG_CLOCK_NOT_FOUND */   "NOT FOUND",
    /* MSG_DISK_OK */           "Disk OK, Read/Write",
    /* MSG_DISK_WRITE_PROTECTED */ "Disk OK, Write Protected",
    /* MSG_DISK_NO_DISK */      "No Disk Present",

    /* Zorro types */
    /* MSG_ZORRO_II */          "ZORRO II",
    /* MSG_ZORRO_III */         "ZORRO III",

    /* Memory types */
    /* MSG_CHIP_RAM */          "CHIP RAM",
    /* MSG_FAST_RAM */          "FAST RAM",
    /* MSG_SLOW_RAM */          "SLOW RAM",
    /* MSG_ROM */               "ROM",
    /* MSG_24BIT_RAM */         "24BIT RAM",
    /* MSG_32BIT_RAM */         "32BIT RAM",

    /* Filesystem types */
    /* MSG_OFS */               "Old File System",
    /* MSG_FFS */               "Fast File System",
    /* MSG_INTL_OFS */          "Intl Old File System",
    /* MSG_INTL_FFS */          "Intl Fast File System",
    /* MSG_DCACHE_OFS */        "DC Old File System",
    /* MSG_DCACHE_FFS */        "DC Fast File System",
    /* MSG_SFS */               "Smart File System",
    /* MSG_PFS */               "Professional File System",
    /* MSG_UNKNOWN_FS */        "Unknown File System",

    /* Requester dialogs */
    /* MSG_ENTER_FILENAME */    "Enter Filename or RETURN",
    /* MSG_MEASURING_SPEED */   "Measuring Speed",

    /* Error messages */
    /* MSG_ERR_NO_IDENTIFY */   "Could not open identify.library v13+",
    /* MSG_ERR_NO_MEMORY */     "Out of memory",
    /* MSG_ERR_NO_SCREEN */     "Could not open screen",
    /* MSG_ERR_NO_WINDOW */     "Could not open window",

    /* Comments based on system speed */
    /* MSG_COMMENT_BLAZING */   "Blazingly fast!",
    /* MSG_COMMENT_VERY_FAST */ "Very fast!",
    /* MSG_COMMENT_FAST */      "Fast system",
    /* MSG_COMMENT_GOOD */      "Good speed",
    /* MSG_COMMENT_CLASSIC */   "Classic Amiga",
    /* MSG_COMMENT_DEFAULT */   "What can I say!",
};

/* Get string by ID (currently just returns default English) */
const char *get_string(LocaleStringID id)
{
    if (id >= 0 && id < MSG_COUNT) {
        return default_strings[id];
    }
    return "???";
}

/* Initialize locale - placeholder for future locale.library support */
BOOL init_locale(void)
{
    /* TODO: Open locale.library and load catalog if available */
    return TRUE;
}

/* Cleanup locale */
void cleanup_locale(void)
{
    /* TODO: Close catalog and locale.library */
}
