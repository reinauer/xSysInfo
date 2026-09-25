// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <hardware/cia.h>
#include <resources/cia.h>
#include <proto/cia.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include "hardware.h"
#include "cpu.h"
#include "probeclock.h"
#include "debug.h"

extern struct ExecBase *SysBase;
extern struct Device *TimerBase;

static BOOL acquired, use_eclock;
static struct Library *resource;
static struct Interrupt timer_interrupt;
static volatile UBYTE *control, *counter_hi, *counter_lo;
static UBYTE timer_bit, shared_mask, saved_control;

BOOL acquire_probe_clock(void)
{
    unsigned chip, bit;

    if (acquired || !TimerBase)
        return FALSE;
    if (SysBase->LibNode.lib_Version >= 36 &&
        TimerBase->dd_Library.lib_Version >= 36) {
        acquired = use_eclock = TRUE;
        return TRUE;
    }

    timer_interrupt.is_Node.ln_Type = NT_INTERRUPT;
    timer_interrupt.is_Node.ln_Name = (char *)"xSysInfo probe clock";
    timer_interrupt.is_Code = ProbeClockInterrupt;
    for (chip = 0; chip < 2; chip++) {
        struct Library *candidate = (struct Library *)
            OpenResource((CONST_STRPTR)(chip ? CIABNAME : CIAANAME));
        volatile struct CIA *cia = (volatile struct CIA *)
            (chip ? 0xbfd000 : 0xbfe001);
        if (!candidate)
            continue;
        for (bit = CIAICRB_TA; bit <= CIAICRB_TB; bit++) {
            /* AddICRVector enables the bit. Mask it before interrupts can
             * run; this timer is polled, including under Kickstart 1.3. */
            Disable();
            if (AddICRVector(candidate, bit, &timer_interrupt)) {
                Enable();
                continue;
            }
            resource = candidate;
            timer_bit = bit;
            AbleICR(resource, 1 << bit);
            control = bit ? &cia->ciacrb : &cia->ciacra;
            counter_hi = bit ? &cia->ciatbhi : &cia->ciatahi;
            counter_lo = bit ? &cia->ciatblo : &cia->ciatalo;
            shared_mask = bit ? CIACRBF_ALARM :
                                CIACRAF_TODIN | CIACRAF_SPMODE;
            saved_control = *control;
            /* Keep serial/TOD controls, but disconnect timer output from
             * the port pins and select E-clock input in one-shot mode. */
            *control = (saved_control & shared_mask) | CIACRAF_RUNMODE;
            SetICR(resource, 1 << bit);
            acquired = TRUE;
            use_eclock = FALSE;
            Enable();
            debug("    probe clock: CIA%c timer %c, %lu Hz\n",
                  'A' + chip, 'A' + bit, hw_info.eclock_freq);
            return TRUE;
        }
    }
    debug("    probe clock: no free CIA interval timer\n");
    return FALSE;
}

void release_probe_clock(void)
{
    if (!acquired)
        return;
    if (!use_eclock) {
        Disable();
        /* Restore timer controls stopped. Preserve any serial/TOD change
         * made by another interrupt while we owned the interval timer. */
        *control = (*control & shared_mask) |
                   (saved_control & ~(shared_mask | CIACRAF_START |
                                      CIACRAF_LOAD));
        SetICR(resource, 1 << timer_bit);
        RemICRVector(resource, timer_bit, &timer_interrupt);
        resource = NULL;
        Enable();
    }
    acquired = use_eclock = FALSE;
}

ULONG read_probe_clock(struct EClockVal *value)
{
    UBYTE hi, lo;
    unsigned tries;

    value->ev_hi = value->ev_lo = 0;
    if (!acquired)
        return 0;
    if (use_eclock)
        return ReadEClock(value);

    /* Interval counters have no TOD-style read latch. Retry if the high
     * byte changes while reading the low byte. Bound retries as well. */
    for (tries = 0; tries < 4; tries++) {
        hi = *counter_hi;
        lo = *counter_lo;
        if (hi == *counter_hi) {
            /* One-shot expiry clears START, even if the counter reloads.
             * Never interpret a too-long probe as a short wrapped one. */
            if (!(*control & CIACRAF_START))
                return 0;
            value->ev_lo = 0xffffUL - (((ULONG)hi << 8) | lo);
            return hw_info.eclock_freq;
        }
    }
    return 0;
}

ULONG start_probe_clock(struct EClockVal *value)
{
    if (acquired && !use_eclock) {
        UBYTE mode = (*control & shared_mask) | CIACRAF_RUNMODE;
        *control = mode;
        SetICR(resource, 1 << timer_bit);
        *counter_lo = 0xff;
        *counter_hi = 0xff;
        *control = mode | CIACRAF_LOAD | CIACRAF_START;
    }
    return read_probe_clock(value);
}
