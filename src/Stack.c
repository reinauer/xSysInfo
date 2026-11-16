// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Stefan Reinauer

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/exec.h>

#define MINSTACK 4096L
#define MAX_PARSE_VALUE 0x7fffffffUL
#define MAX_PARSE_DIV10 (MAX_PARSE_VALUE / 10)

struct ExecBase *SysBase;

/* For Version; the command ships with each xSysInfo release. */
static const char version_tag[] __attribute__((used)) =
    "$VER: Stack " XSYSINFO_VERSION " (" XSYSINFO_DATE ")";

/*
 * DOS enters commands with A0 pointing at the argument line and D0 holding
 * its byte length.  Providing the entry wrapper here avoids libc startup.
 */
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    move.l d0,-(sp)\n"
    "    move.l a0,-(sp)\n"
    "    jsr _stack_main\n"
    "    addq.l #8,sp\n"
    "    rts\n");

static int is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static struct ExecBase *get_sysbase(void)
{
    struct ExecBase *sysbase;

    __asm__ volatile ("move.l 4.w,%0" : "=a"(sysbase));
    return sysbase;
}

static ULONG decimal_step(ULONG value)
{
    __asm__ volatile (
        "move.l %0,%%d0\n"
        "add.l %%d0,%%d0\n"
        "add.l %0,%0\n"
        "add.l %0,%0\n"
        "add.l %0,%0\n"
        "add.l %%d0,%0"
        : "+d"(value)
        :
        : "d0", "cc");

    return value;
}

static ULONG parse_stack_size(const char *line, LONG length)
{
    ULONG value = 0;
    int saw_digit = 0;

    if (!line || length <= 0)
        return 0;

    while (length > 0 && is_space(*line)) {
        line++;
        length--;
    }

    while (length > 0 && *line >= '0' && *line <= '9') {
        ULONG digit = (ULONG)(*line - '0');

        if (value > MAX_PARSE_DIV10)
            return 0;
        value = decimal_step(value);

        if (value > MAX_PARSE_VALUE - digit)
            return 0;

        value += digit;
        saw_digit = 1;
        line++;
        length--;
    }

    while (length > 0 && is_space(*line)) {
        line++;
        length--;
    }

    if (!saw_digit || length > 0)
        return 0;

    return value;
}

__attribute__((used))
LONG stack_main(const char *cmdline, LONG cmdlen)
{
    struct Process *process;
    struct CommandLineInterface *cli;
    ULONG stacksize;
    APTR stackptr;

    SysBase = get_sysbase();

    stacksize = parse_stack_size(cmdline, cmdlen);
    if (stacksize < MINSTACK)
        return RETURN_FAIL;

    process = (struct Process *)FindTask(0);
    if (!process || !process->pr_CLI)
        return RETURN_FAIL;

    stackptr = AllocMem((ULONG)stacksize, MEMF_ANY);
    if (!stackptr)
        return RETURN_FAIL;

    FreeMem(stackptr, (ULONG)stacksize);

    cli = (struct CommandLineInterface *)BADDR(process->pr_CLI);
    cli->cli_DefaultStack = (LONG)(stacksize >> 2);

    return RETURN_OK;
}
