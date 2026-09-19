// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#include <devices/timer.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>

#include "clock.h"
#include "hardware.h"

static struct MsgPort *clock_port;
static struct timerequest *clock_request;
static BOOL clock_open, clock_pending;

static UWORD month_days(UWORD year, UWORD month)
{
    static const UBYTE days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 &&
                             (year % 100 != 0 || year % 400 == 0));
}

static BOOL valid_date(const struct ClockData *date)
{
    return date->year >= 1978 && date->year <= 9999 &&
           date->month >= 1 && date->month <= 12 && date->mday >= 1 &&
           date->mday <= month_days(date->year, date->month) &&
           date->hour < 24 && date->min < 60 && date->sec < 60;
}

static UWORD from_bcd(UBYTE value)
{
    if ((value & 15) > 9 || (value >> 4) > 9)
        return 0xffff;
    return (value >> 4) * 10 + (value & 15);
}

/* Snapshot the detected RTC without changing its date or configuration.
 * ReadBattClock() can reset an invalid clock. Diagnostics must instead
 * leave it intact and report that its date cannot be read.
 * Register layouts/hold protocols: OKI MSM6242B, Ricoh RP5C01A and
 * Mostek MK48T02 data sheets. All temporary hold/bank bits are restored.
 */
BOOL read_hardware_time(struct ClockData *date)
{
    volatile UBYTE *rtc = (volatile UBYTE *)RTC_BASE;
    UBYTE fields[13], control, mode = 1;
    ULONG i, attempt;
    BOOL ready = FALSE;
    UWORD year;

    if (hw_info.clock_type == CLOCK_MK48T02) {
        rtc = (volatile UBYTE *)0xDC0FF1;
        Disable();
        control = rtc[0];
        if (!(control & 0x80)) { /* Do not interrupt a clock write. */
            rtc[0] = control | 0x40;
            for (i = 0; i < 7; i++)
                fields[i] = rtc[2 + i * 2];
            rtc[0] = control;
            ready = TRUE;
        }
        Enable();
        if (!ready)
            return FALSE;
        date->sec = from_bcd(fields[0] & 0x7f);
        date->min = from_bcd(fields[1] & 0x7f);
        date->hour = from_bcd(fields[2] & 0x3f);
        date->mday = from_bcd(fields[4] & 0x3f);
        date->month = from_bcd(fields[5] & 0x1f);
        year = from_bcd(fields[6]);
    } else if (hw_info.clock_type == CLOCK_MSM6242 ||
               hw_info.clock_type == CLOCK_RP5C01) {
        for (attempt = 0; attempt < 64 && !ready; attempt++) {
            Disable();
            control = rtc[RTC_REG_D] & RTC_MASK;
            if (hw_info.clock_type == CLOCK_MSM6242) {
                /* HOLD must be acquired while BUSY is clear. Write one
                 * to IRQ FLAG to preserve pending interrupts, and never
                 * repeat a 30-second adjustment command. */
                rtc[RTC_REG_D] = 5;
                ready = !(rtc[RTC_REG_D] & 2);
                mode = (rtc[RTC_REG_F] & 4) != 0;
            } else {
                /* Stop the counters briefly and select the format bank,
                 * then the time bank. The divider keeps counting. */
                rtc[RTC_REG_D] = (control & 4) | 1;
                mode = rtc[RTC_REG_A] & 1;
                rtc[RTC_REG_D] = control & 4;
                ready = TRUE;
            }
            if (ready) {
                for (i = 0; i < 13; i++)
                    fields[i] = rtc[3 + i * 4] & RTC_MASK;
            }
            rtc[RTC_REG_D] = hw_info.clock_type == CLOCK_MSM6242 ?
                             (control & 1) | 4 : control;
            Enable();
        }
        if (!ready)
            return FALSE;
        date->sec = from_bcd((fields[1] << 4) | fields[0]);
        date->min = from_bcd((fields[3] << 4) | fields[2]);
        date->hour = from_bcd(((fields[5] & (mode ? 3 : 1)) << 4) |
                             fields[4]);
        if (!mode) {
            if (date->hour < 1 || date->hour > 12)
                return FALSE;
            date->hour = date->hour % 12 +
                ((fields[5] & (hw_info.clock_type == CLOCK_MSM6242 ? 4 : 2)) ?
                 12 : 0);
        }
        i = hw_info.clock_type == CLOCK_RP5C01 ? 7 : 6;
        date->mday = from_bcd((fields[i + 1] << 4) | fields[i]);
        date->month = from_bcd((fields[i + 3] << 4) | fields[i + 2]);
        year = from_bcd((fields[i + 5] << 4) | fields[i + 4]);
    } else {
        return FALSE;
    }
    if (year > 99)
        return FALSE;
    /* Match AmigaOS's two-digit RTC year window: 1978 through 2077. */
    date->year = year + (year < 78 ? 2000 : 1900);
    date->wday = 0; /* Not used for display. */
    return valid_date(date);
}

void cleanup_clock_refresh(void)
{
    if (clock_pending) {
        AbortIO((struct IORequest *)clock_request);
        WaitIO((struct IORequest *)clock_request);
        clock_pending = FALSE;
    }
    if (clock_open) {
        CloseDevice((struct IORequest *)clock_request);
        clock_open = FALSE;
    }
    if (clock_request) {
        DeleteExtIO((struct IORequest *)clock_request);
        clock_request = NULL;
    }
    if (clock_port) {
        DeletePort(clock_port);
        clock_port = NULL;
    }
}

ULONG clock_refresh_signal(BOOL enabled)
{
    if (!enabled) {
        cleanup_clock_refresh();
        return 0;
    }
    if (!clock_open) {
        clock_port = CreatePort(NULL, 0);
        if (clock_port)
            clock_request = (struct timerequest *)CreateExtIO(
                clock_port, sizeof(*clock_request));
        if (!clock_request || OpenDevice((CONST_STRPTR)"timer.device",
                UNIT_VBLANK, (struct IORequest *)clock_request, 0)) {
            cleanup_clock_refresh();
            return 0;
        }
        clock_open = TRUE;
    }
    if (!clock_pending) {
        clock_request->tr_node.io_Command = TR_ADDREQUEST;
        clock_request->tr_time.tv_secs = 1;
        clock_request->tr_time.tv_micro = 0;
        SendIO((struct IORequest *)clock_request);
        clock_pending = TRUE;
    }
    return 1UL << clock_port->mp_SigBit;
}

BOOL clock_refresh_ready(void)
{
    if (!clock_pending || !CheckIO((struct IORequest *)clock_request))
        return FALSE;
    WaitIO((struct IORequest *)clock_request);
    clock_pending = FALSE;
    return TRUE;
}
