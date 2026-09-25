// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

/* Optional A3000 WD33C93 diagnostic, never called by hardware detection.
 * Register definitions/reset protocol: WD33C93A data sheet, sections
 * 6.2, 6.3 and 7.4; WD33C93B data sheet (RAF and queue-tag register).
 * https://bitsavers.org/components/westernDigital/79_000199_WDC_WD33C93A_SCSI_Bus_Interface_Controller.pdf
 *
 * Measure the difference between two selection timeouts. The documented
 * relation is T(ms) = register * 80 / clock(MHz). Subtracting durations
 * cancels reset/selection overhead without a chip-specific correction.
 * No SCSI command descriptor, data transfer or SCSI bus reset is issued.
 */

#include <stdio.h>
#include <string.h>
#include <devices/timer.h>
#include <proto/exec.h>

#include "hardware.h"
#include "wdprobe.h"
#include "probeclock.h"
#include "locale_str.h"
#include "debug.h"

#define WD_INDEX ((volatile UBYTE *)0xdd0049)
#define WD_DATA  ((volatile UBYTE *)0xdd0043)
#define WD_OWN_ID 0x00
#define WD_CONTROL 0x01
#define WD_TIMEOUT 0x02
#define WD_CDB1 0x03
#define WD_LUN 0x0f
#define WD_PHASE 0x10
#define WD_SYNC 0x11
#define WD_COUNT 0x12
#define WD_DEST 0x15
#define WD_SOURCE 0x16
#define WD_STATUS 0x17
#define WD_COMMAND 0x18
#define WD_QUEUE_TAG 0x1a
#define WD_INVALID 0x1e
#define WD_INT 0x80
#define WD_CIP 0x10
#define WD_RESET 0x00
#define WD_SELECT 0x07
#define WD_TIMEOUT_STATUS 0x42
#define WD_POLL_LIMIT 65536UL

WDProbeInfo wd_info;

static ULONG clock_rate, clock_start;
static BOOL clock_failed;
static BOOL restore_failed;

static UBYTE wd_read(UBYTE reg)
{
    *WD_INDEX = reg;
    return *WD_DATA;
}

static void wd_write(UBYTE reg, UBYTE value)
{
    *WD_INDEX = reg;
    *WD_DATA = value;
}

/* The shared probe clock works with interrupts disabled. All masked
 * work including recovery finishes within 80 ms (one CIA rollover).
 * A poll limit bounds recovery even with a broken/frozen timer. No DOS
 * calls here. The first 60 ms are for probing, the final 20 for recovery. */
static ULONG elapsed(void)
{
    struct EClockVal now;
    if (read_probe_clock(&now) != clock_rate)
        clock_failed = TRUE;
    return now.ev_lo - clock_start;
}

static BOOL wait_status(UBYTE *status, ULONG deadline, BOOL recovering)
{
    ULONG polls;
    for (polls = 0; polls < WD_POLL_LIMIT; polls++) {
        UBYTE asr = *SDMAC_WD_ASR;
        if (asr & WD_ASR_RESERVED)
            return FALSE;
        if ((asr & (WD_INT | WD_CIP)) == WD_INT) {
            *status = wd_read(WD_STATUS);
            return TRUE;
        }
        if ((!clock_failed && elapsed() >= deadline) ||
            (clock_failed && !recovering))
            return FALSE;
    }
    return FALSE;
}

static BOOL reset_controller(UBYTE own_id, ULONG deadline, UBYTE *status,
                             BOOL recovering)
{
    wd_write(WD_OWN_ID, own_id);
    wd_write(WD_COMMAND, WD_RESET);
    return wait_status(status, deadline, recovering) &&
           (*status == 0 || *status == 1);
}

static BOOL selection_ticks(UBYTE timeout, ULONG deadline, ULONG *ticks)
{
    ULONG start;
    UBYTE status;

    if (*SDMAC_WD_ASR != 0 || elapsed() >= deadline || clock_failed)
        return FALSE;
    wd_write(WD_TIMEOUT, timeout);
    start = elapsed();
    wd_write(WD_COMMAND, WD_SELECT);
    if (!wait_status(&status, deadline, FALSE))
        return FALSE;
    *ticks = elapsed() - start;
    return !clock_failed && status == WD_TIMEOUT_STATUS && *ticks != 0;
}

WDProbeStatus probe_wd_controller(void)
{
    UBYTE saved[WD_SOURCE + 1], queue_tag = 0, status, old_timeout;
    ULONG short_ticks, long_ticks, rates[2] = {0, 0}, deadline;
    struct EClockVal now;
    WDProbeInfo result;
    WDProbeStatus outcome = WD_PROBE_BUSY;
    BOOL changed = FALSE, restored = TRUE;
    unsigned i;

    if (restore_failed)
        return WD_PROBE_RESTORE_FAILED;
    if (!hw_info.sdmac_present || hw_info.gary_type != FAT_GARY ||
        hw_info.ncr_type != NCR_NONE)
        return WD_PROBE_NOT_APPLICABLE;
    if (!acquire_probe_clock())
        return WD_PROBE_UNAVAILABLE;
    memset(&result, 0, sizeof(result));
    clock_failed = FALSE;

    Forbid();
    Disable();
    clock_rate = start_probe_clock(&now);
    if (clock_rate < 700000 || clock_rate > 720000) {
        Enable();
        Permit();
        release_probe_clock();
        return WD_PROBE_UNAVAILABLE;
    }
    clock_start = now.ev_lo;
    deadline = clock_rate * 60 / 1000;
    old_timeout = *(volatile UBYTE *)FAT_GARY_TIME_OUT_REG;
    *(volatile UBYTE *)FAT_GARY_TIME_OUT_REG = FAT_GARY_TIME_OUT_DSACK;

    /* Refuse busy, pending-interrupt, parity-error and open-bus states
     * before touching the WD selector or acknowledging any interrupt. */
    if (*SDMAC_WD_ASR != 0 || *SDMAC_ISTR != SDMAC_ISTR_FIFOE)
        goto done;
    outcome = WD_PROBE_UNAVAILABLE;
    if (wd_read(WD_INVALID) != 0xff)
        goto done;
    for (i = 0; i <= WD_SOURCE; i++)
        saved[i] = wd_read(i);

    /* OWN_ID also holds CDB length during I/O. Only accept the A3000
     * reset image (divide by 3), never reset with a command length as
     * the host ID. Where BattMem is valid, also require its host ID.
     * A disconnected/incomplete command is not an idle controller. */
    if ((saved[WD_OWN_ID] & 0xc0) != 0x40 ||
        (hw_info.battMemData.valid_data &&
         (saved[WD_OWN_ID] & 7) != hw_info.battMemData.scsi_id))
        goto done;
    outcome = WD_PROBE_BUSY;
    if ((saved[WD_PHASE] != 0 && saved[WD_PHASE] != 0x60) ||
        saved[WD_COUNT] || saved[WD_COUNT + 1] || saved[WD_COUNT + 2] ||
        *SDMAC_WD_ASR != 0 || *SDMAC_ISTR != SDMAC_ISTR_FIFOE)
        goto done;

    queue_tag = wd_read(WD_QUEUE_TAG);
    outcome = WD_PROBE_FAILED;
    changed = TRUE;
    if (!reset_controller(saved[WD_OWN_ID] | 0x28, deadline, &status, FALSE))
        goto recover;
    result.chip = status == 0 ? WD_33C93 : WD_33C93A;
    if (result.chip == WD_33C93A) {
        /* RAF reset places microcode in CDB1 on supporting revisions.
         * Zero is not a software-readable microcode revision. */
        result.microcode = wd_read(WD_CDB1);
        if (result.microcode == 0xff)
            result.microcode = 0;
        wd_write(WD_QUEUE_TAG, 0x55);
        if (wd_read(WD_QUEUE_TAG) == 0x55) {
            wd_write(WD_QUEUE_TAG, 0xaa);
            if (wd_read(WD_QUEUE_TAG) == 0xaa)
                result.chip = WD_33C93B;
        }
    }
    result.control = saved[WD_CONTROL];
    result.timeout = saved[WD_TIMEOUT];
    result.sync = saved[WD_SYNC];

    /* Selecting our own initiator ID cannot select a disk on a correctly
     * configured bus. Do not assume the host is ID 7. Keep DMA and
     * selection/reselection response disabled throughout measurement. */
    wd_write(WD_CONTROL, 0x0c);
    wd_write(WD_SOURCE, 0);
    wd_write(WD_DEST, saved[WD_OWN_ID] & 7);
    wd_write(WD_LUN, 0);
    wd_write(WD_SYNC, 0);
    outcome = WD_PROBE_CLOCK_FAILED;
    for (i = 0; i < 2; i++) {
        if (!selection_ticks(1, deadline, &short_ticks) ||
            !selection_ticks(3, deadline, &long_ticks) ||
            long_ticks <= short_ticks)
            goto recover;
        rates[i] = clock_rate * 160 / (long_ticks - short_ticks);
        if (rates[i] < 5000 || rates[i] > 50000)
            goto recover;
    }
    /* Refuse timing interference or emulators without clock-accurate
     * selection timeouts. Keep the chip/microcode result independently. */
    if ((rates[0] > rates[1] ? rates[0] - rates[1] : rates[1] - rates[0]) >
        rates[0] / 50)
        goto recover;
    result.clock_khz = (rates[0] + rates[1] + 1) / 2;
    outcome = WD_PROBE_OK;

recover:
    /* Always reset back into the saved operating mode, then restore ALL
     * writable configuration/CDB/count registers, not the command or
     * data ports. Never replay a saved command. Clear only our own IRQs.
     * The selector itself is write-only; do not mistake ASR for it. */
    if (*SDMAC_WD_ASR & WD_INT)
        (void)wd_read(WD_STATUS);
    restored = reset_controller(saved[WD_OWN_ID], clock_rate * 80 / 1000,
                                &status, TRUE);
    if (restored)
        restored = status == ((saved[WD_OWN_ID] & 8) ? 1 : 0);
    if (restored) {
        for (i = 0; i < WD_SOURCE; i++)
            wd_write(i, saved[i]);
        /* Restore this even if identification failed after a reset.
         * Older parts ignore the unimplemented queue-tag register. */
        wd_write(WD_QUEUE_TAG, queue_tag);
        wd_write(WD_SOURCE, saved[WD_SOURCE]);
        /* SOURCE's low bits describe the last reselecting target; they
         * are read-only. Check its writable enable bits separately. */
        for (i = 0; i <= WD_DEST; i++) {
            /* TARGET LUN includes read-only status/validity bits. */
            if (i != WD_LUN && wd_read(i) != saved[i])
                restored = FALSE;
        }
        if (wd_read(WD_QUEUE_TAG) != queue_tag ||
            (wd_read(WD_SOURCE) & 0xe0) != (saved[WD_SOURCE] & 0xe0) ||
            *SDMAC_WD_ASR != 0)
            restored = FALSE;
    }
    if (!restored) {
        restore_failed = TRUE;
        outcome = WD_PROBE_RESTORE_FAILED;
        memset(&result, 0, sizeof(result));
    }
done:
    *(volatile UBYTE *)FAT_GARY_TIME_OUT_REG = old_timeout;
    Enable();
    Permit();
    release_probe_clock();
    if (changed)
        wd_info = result;
    /* A redirected debug stream could itself access the failed disk. */
    if (!restore_failed)
        debug("    wdprobe: status=%d chip=%d microcode=%02x clock=%lu kHz "
              "samples=%lu/%lu restored=%d\n", outcome, result.chip,
              result.microcode, result.clock_khz, rates[0], rates[1], restored);
    return outcome;
}

void format_wd_detail(unsigned detail, char *buffer, ULONG size)
{
    unsigned mode, offset, period;
    ULONG rate;
    const char *value = get_string(MSG_NA);

    switch (detail) {
    case WD_DETAIL_MICROCODE:
        if (wd_info.microcode) {
            snprintf(buffer, size, "%02X", wd_info.microcode);
            return;
        }
        break;
    case WD_DETAIL_CLOCK:
        if (wd_info.clock_khz) {
            rate = (wd_info.clock_khz + 50) / 100;
            snprintf(buffer, size, "%lu.%lu MHz", rate / 10, rate % 10);
            return;
        }
        break;
    case WD_DETAIL_MODE:
        mode = wd_info.control >> 5;
        if (mode == 0) value = get_string(MSG_WD_POLLED);
        if (mode == 1) value = get_string(MSG_RAMSEY_BURST);
        if (mode == 2) value = get_string(MSG_WD_BUS);
        if (mode == 4) value = "DMA";
        break;
    case WD_DETAIL_TIMEOUT:
        if (!wd_info.timeout) value = get_string(MSG_OFF);
        else if (wd_info.clock_khz) {
            snprintf(buffer, size, "%lu ms",
                     (wd_info.timeout * 80000UL + wd_info.clock_khz / 2) /
                     wd_info.clock_khz);
            return;
        }
        break;
    case WD_DETAIL_SYNC:
        offset = wd_info.sync & 15;
        period = (wd_info.sync >> 4) & 7;
        if (period < 2 && wd_info.chip != WD_33C93) period = 8;
        if (!offset) value = get_string(MSG_WD_ASYNC);
        else if (offset <= 12 && period && wd_info.clock_khz) {
            rate = 2 * wd_info.clock_khz / (3 * period);
            snprintf(buffer, size, "%lu.%02lu MHz /%u",
                     rate / 1000, (rate % 1000) / 10, offset);
            return;
        }
        break;
    }
    copy_string(buffer, value, size);
}
