/*
 * xSysInfo - Locale string definitions
 * All user-visible strings are defined here for future localization
 */

#ifndef LOCALE_STR_H
#define LOCALE_STR_H

/* String IDs for localization */
typedef enum {
    /* Header strings (app name/URL are hardcoded, not localized) */
    MSG_TAGLINE,
    MSG_CONTACT_LABEL,

    /* Section headers */
    MSG_SYSTEM_SOFTWARE,
    MSG_INTERNAL_HARDWARE,
    MSG_SPEED_COMPARISONS,
    MSG_MEMORY_INFO,
    MSG_BOARDS_INFO,
    MSG_DRIVES_INFO,
    MSG_SCSI_INFO,

    /* Software type cycle */
    MSG_LIBRARIES,
    MSG_DEVICES,
    MSG_RESOURCES,

    /* Scale toggle */
    MSG_EXPAND,
    MSG_SHRINK,

    /* Hardware labels */
    MSG_CLOCK,
    MSG_DMA_GFX,
    MSG_MODE,
    MSG_DISPLAY,
    MSG_CPU_MHZ,
    MSG_FPU,
    MSG_MMU,
    MSG_VBR,
    MSG_COMMENT,
    MSG_HORIZ_KHZ,
    MSG_ECLOCK_HZ,
    MSG_RAMSEY_REV,
    MSG_GARY_REV,
    MSG_CARD_SLOT,
    MSG_VERT_HZ,
    MSG_SUPPLY_HZ,

    /* Cache labels */
    MSG_ICACHE,
    MSG_DCACHE,
    MSG_IBURST,
    MSG_DBURST,
    MSG_CBACK,

    /* Speed comparison labels */
    MSG_DHRYSTONES,
    MSG_MIPS,
    MSG_MFLOPS,
    MSG_MEM_SPEED_UNIT,

    /* Reference system names */
    MSG_REF_A600,
    MSG_REF_B2000,
    MSG_REF_A1200,
    MSG_REF_A2500,
    MSG_REF_A3000,
    MSG_REF_A4000,
    MSG_REF_YOU,

    /* Memory view labels */
    MSG_START_ADDRESS,
    MSG_END_ADDRESS,
    MSG_TOTAL_SIZE,
    MSG_MEMORY_TYPE,
    MSG_PRIORITY,
    MSG_LOWER_BOUND,
    MSG_UPPER_BOUND,
    MSG_FIRST_ADDRESS,
    MSG_AMOUNT_FREE,
    MSG_LARGEST_BLOCK,
    MSG_NUM_CHUNKS,
    MSG_NODE_NAME,
    MSG_MEMORY_SPEED,

    /* Drives view labels */
    MSG_DISK_ERRORS,
    MSG_UNIT_NUMBER,
    MSG_DISK_STATE,
    MSG_TOTAL_BLOCKS,
    MSG_BLOCKS_USED,
    MSG_BYTES_PER_BLOCK,
    MSG_DISK_TYPE,
    MSG_VOLUME_NAME,
    MSG_DEVICE_NAME,
    MSG_SURFACES,
    MSG_SECTORS_PER_SIDE,
    MSG_RESERVED_BLOCKS,
    MSG_LOWEST_CYLINDER,
    MSG_HIGHEST_CYLINDER,
    MSG_NUM_BUFFERS,
    MSG_SPEED,

    /* Boards view labels */
    MSG_BOARD_ADDRESS,
    MSG_BOARD_SIZE,
    MSG_BOARD_TYPE,
    MSG_PRODUCT,
    MSG_MANUFACTURER,
    MSG_SERIAL_NO,

    /* Button labels */
    MSG_BTN_QUIT,
    MSG_BTN_MEMORY,
    MSG_BTN_DRIVES,
    MSG_BTN_BOARDS,
    MSG_BTN_SPEED,
    MSG_BTN_PRINT,
    MSG_BTN_PREV,
    MSG_BTN_NEXT,
    MSG_BTN_EXIT,
    MSG_BTN_SCSI,

    /* Status and values */
    MSG_NA,
    MSG_NONE,
    MSG_YES,
    MSG_NO,
    MSG_IN_USE,
    MSG_CLOCK_FOUND,
    MSG_CLOCK_NOT_FOUND,
    MSG_DISK_OK,
    MSG_DISK_WRITE_PROTECTED,
    MSG_DISK_NO_DISK,

    /* Zorro types */
    MSG_ZORRO_II,
    MSG_ZORRO_III,

    /* Memory types */
    MSG_CHIP_RAM,
    MSG_FAST_RAM,
    MSG_SLOW_RAM,
    MSG_ROM,
    MSG_24BIT_RAM,
    MSG_32BIT_RAM,

    /* Filesystem types */
    MSG_OFS,
    MSG_FFS,
    MSG_INTL_OFS,
    MSG_INTL_FFS,
    MSG_DCACHE_OFS,
    MSG_DCACHE_FFS,
    MSG_SFS,
    MSG_PFS,
    MSG_UNKNOWN_FS,

    /* Requester dialogs */
    MSG_ENTER_FILENAME,
    MSG_MEASURING_SPEED,

    /* Error messages */
    MSG_ERR_NO_IDENTIFY,
    MSG_ERR_NO_MEMORY,
    MSG_ERR_NO_SCREEN,
    MSG_ERR_NO_WINDOW,

    /* Comments based on system speed */
    MSG_COMMENT_BLAZING,
    MSG_COMMENT_VERY_FAST,
    MSG_COMMENT_FAST,
    MSG_COMMENT_GOOD,
    MSG_COMMENT_CLASSIC,
    MSG_COMMENT_DEFAULT,

    MSG_COUNT  /* Total number of strings */
} LocaleStringID;

/* Get localized string by ID */
const char *get_string(LocaleStringID id);

/* Initialize locale (returns TRUE if successful) */
BOOL init_locale(void);

/* Cleanup locale */
void cleanup_locale(void);

#endif /* LOCALE_STR_H */
