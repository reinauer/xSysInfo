; SPDX-License-Identifier: BSD-2-Clause
; SPDX-FileCopyrightText: 2025 Stefan Reinauer

; TinySetPatch.S
; Tiny SetPatch replacement
; Implements: 680x0 Support, AGA Graphics, Data Cache
;
; Usage: TinySetPatch [QUIET]
;   QUIET - Suppress all output (for startup-sequence use)

;   Patch Table System:
;   - ProcessPatchTable() iterates through patches in 0x0BAD format
;   - Each patch returns: 0=skip, 1=success, -1=failure
;   - Summary shows count of applied patches
; 
;   Implemented Patches:
; 
;   1. Patch_680x0Support (Patch 0):
;     - Detects CPU type (68000-68060)
;     - Sets cumulative AttnFlags (060 sets all lower CPU flags)
;     - Fixes Vector 7 alignment
;     - Installs exception vectors 11, 60, 61 for 68060
;     - Probes CACR bit 14
;     - Loads appropriate 680x0.library with cache management
;   2. Patch_AGAGraphics (Patch 1):
;     - Detects AGA via LISAID register ($DFF07C)
;     - Enables 64-bit fetch mode via FMODE ($DFF1FC)
;   3. Patch_DataCache (Patch 2):
;     - CPU-specific cache enabling:
;         - 68020/030: I-cache and D-cache bits
;       - 68040: $80008000 pattern
;       - 68060: Full cache/branch/store buffer
; 
;   Exception Handlers:
;   - FLineHandler (Vector 11) - chains to old handler or skips F-line instructions
;   - UnimpFPHandler (Vector 60) - chains or skips unimplemented FP ops
;   - UnimpIntHandler (Vector 61) - chains or skips MOVEP/CAS/CAS2/64-bit MUL/DIV
; 
;   Error Handling:
;   - PrintHexLong displays IoErr codes in hex when library loading fails
;   - Each patch reports its status independently
; 

    INCDIR  "include:"
    INCLUDE "exec/types.i"
    INCLUDE "exec/execbase.i"
    INCLUDE "exec/libraries.i"
    INCLUDE "exec/nodes.i"
    INCLUDE "exec/semaphores.i"
    INCLUDE "lvo/exec_lib.i"
    INCLUDE "lvo/dos_lib.i"

; --- Critical Constants ---
AbsExecBase     EQU 4
EB_AttnFlags    EQU 296

; Patch IDs (like SetPatch's 0x0BAD xxxx)
PATCH_ID_BASE   EQU $0BAD0000
PATCH_680X0     EQU 0           ; Generic 680x0 Support Code Loaded
PATCH_AGA       EQU 1           ; Enabled Advanced Graphics Modes
PATCH_DCACHE    EQU 2           ; Enabled data cache

; Vector offsets
VEC_FLINE       EQU $2C         ; Vector 11 - F-Line Emulator
VEC_UNIMP_FP    EQU $F0         ; Vector 60 - Unimplemented FP
VEC_UNIMP_INT   EQU $F4         ; Vector 61 - Unimplemented Integer

; AGA registers
LISAID          EQU $DFF07C
FMODE           EQU $DFF1FC
AGA_LISA_ID     EQU $00F8

; SetPatch semaphore structure offsets (extends SignalSemaphore)
SSP_PATCHLIST   EQU 46              ; MinList of patches (0x2E)
SSP_PATCHCOUNT  EQU 58              ; Patch count word (0x3A)
SSP_MAXPATCHES  EQU 60              ; Max patches word (0x3C)
SSP_NAMEBUF     EQU 62              ; Name buffer start (0x3E)
SSP_SIZE        EQU 80              ; Total allocation size

; Debug options - set to 1 to enable patch-by-patch debugging
DEBUG_PATCHES   EQU 0               ; Set to 1 to prompt before each patch

    SECTION Code,CODE

; =============================================================
; MAIN ENTRY POINT
; =============================================================
Start:
    ; Save command line arguments (D0=length, A0=pointer)
    ; Must be done BEFORE any other operations destroy these registers
    lea     ArgLength(pc),a1
    move.l  d0,(a1)
    lea     ArgPointer(pc),a1
    move.l  a0,(a1)

    movem.l d2-d7/a2-a6,-(sp)

    ; --- Setup DOS for output ---
    move.l  AbsExecBase.w,a6
    lea     DOSName(pc),a1
    moveq   #0,d0
    jsr     _LVOOpenLibrary(a6)
    tst.l   d0
    beq     .NoDOS

    lea     DOSBase(pc),a0
    move.l  d0,(a0)

    ; --- Parse command line arguments ---
    bsr     ParseArgs

    lea     MsgStart(pc),a0
    bsr     PrintString

    ; --- Check if already installed via semaphore ---
    move.l  AbsExecBase.w,a6
    jsr     _LVOForbid(a6)
    lea     SemaphoreName(pc),a1
    jsr     _LVOFindSemaphore(a6)
    tst.l   d0
    bne     .AlreadyInstalledPermit

    ; --- Create and add the SetPatch semaphore ---
    ; The SetPatch semaphore is an extended SignalSemaphore structure that
    ; 68060.library and other tools check for to detect if SetPatch has run.
    ;
    ; Structure layout (from original SetPatch 45.15):
    ;   Offset  Size  Description
    ;   0x00    46    SignalSemaphore (standard Exec structure)
    ;   0x2E    12    MinList - list of installed patches
    ;                   0x2E: lh_Head (points to lh_Tail initially)
    ;                   0x32: lh_Tail (NULL)
    ;                   0x36: lh_TailPred (points to lh_Head initially)
    ;   0x3A    2     Patch count (word) - non-zero means "installed"
    ;                   Set to 0x2D (45) by SetPatch
    ;   0x3C    2     Max patches (word) - set to 0x0F (15)
    ;   0x3E    var   Name buffer - contains "« SetPatch »" string
    ;
    ; Total size: 64 + strlen("« SetPatch »") + 1 = ~77 bytes, allocate 80
    moveq   #SSP_SIZE,d0
    move.l  #$10001,d1              ; MEMF_PUBLIC|MEMF_CLEAR
    jsr     _LVOAllocMem(a6)
    tst.l   d0
    beq     .SemaAllocFail
    move.l  d0,a2                   ; A2 = semaphore

    ; Initialize the semaphore
    move.l  a2,a0
    jsr     _LVOInitSemaphore(a6)

    ; Set up node fields
    move.b  #NT_SIGNALSEM,LN_TYPE(a2)
    move.b  #15,LN_PRI(a2)          ; Priority 15 like original

    ; Copy name to end of structure
    lea     SSP_NAMEBUF(a2),a0
    move.l  a0,LN_NAME(a2)          ; Point name to our buffer
    lea     SemaphoreName(pc),a1
.CopyName:
    move.b  (a1)+,(a0)+
    bne.s   .CopyName

    ; Initialize the MinList for patch list
    lea     SSP_PATCHLIST(a2),a0
    move.l  a0,(a0)                 ; lh_Head = &lh_Tail
    addq.l  #4,(a0)
    clr.l   4(a0)                   ; lh_Tail = NULL
    lea     SSP_PATCHLIST(a2),a1
    move.l  a1,8(a0)                ; lh_TailPred = &lh_Head

    ; Set patch count fields
    move.w  #$2D,SSP_PATCHCOUNT(a2) ; Patch count (45 = max patches)
    move.w  #$0F,SSP_MAXPATCHES(a2) ; Max patches (15)

    ; Save semaphore pointer
    lea     SetPatchSemaPtr(pc),a0
    move.l  a2,(a0)

    ; Add the semaphore to system list
    move.l  a2,a1
    jsr     _LVOAddSemaphore(a6)
    jsr     _LVOPermit(a6)
    bra.s   .SemaDone

.SemaAllocFail:
    jsr     _LVOPermit(a6)
    ; Continue anyway, but 68060.library won't find the semaphore

.SemaDone:

    ; --- Detect CPU ---
    lea     MsgDetect(pc),a0
    bsr     PrintString

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    bsr     DetectCPU
    move.l  d0,d7               ; D7 = CPU type (0-6)
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    ; Save CPU type globally
    lea     DetectedCPU(pc),a0
    move.l  d7,(a0)

    bsr     PrintCPUFound

    ; --- Process Patch Table ---
    lea     MsgProcessing(pc),a0
    bsr     PrintString

    bsr     ProcessPatchTable

    ; --- Done ---
    lea     MsgDone(pc),a0
    bsr     PrintString

.Cleanup:
    move.l  AbsExecBase.w,a6
    move.l  DOSBase(pc),a1
    jsr     _LVOCloseLibrary(a6)
    movem.l (sp)+,d2-d7/a2-a6
    moveq   #0,d0
    rts

.NoDOS:
    movem.l (sp)+,d2-d7/a2-a6
    moveq   #20,d0
    rts

.AlreadyInstalledPermit:
    jsr     _LVOPermit(a6)
.AlreadyInstalled:
    lea     MsgAlready(pc),a0
    bsr     PrintString
    bra     .Cleanup

; =============================================================
; PATCH TABLE PROCESSING
; =============================================================
; Patch table format:
;   dc.l    PATCH_ID_BASE+patch_number
;   dc.l    patch_handler_address
;   ...
;   dc.l    0   ; End marker

ProcessPatchTable:
    movem.l d0-d7/a0-a6,-(sp)

    lea     PatchTable(pc),a2
    moveq   #0,d6               ; Patch success counter

.PatchLoop:
    move.l  (a2)+,d0            ; Get patch ID
    beq     .PatchDone          ; End of table

    move.l  (a2)+,a0            ; Get handler address

    ; Extract patch number from ID
    move.l  d0,d1
    sub.l   #PATCH_ID_BASE,d1
    bmi     .PatchLoop          ; Invalid ID, skip

    IFNE    DEBUG_PATCHES
    ; Debug mode: prompt before each patch
    movem.l d0-d1/a0-a2,-(sp)
    lea     MsgDebugNext(pc),a0
    bsr     PrintString
    ; Print patch number
    move.l  d1,d0
    add.b   #'0',d0
    lea     TempChar(pc),a0
    move.b  d0,(a0)
    clr.b   1(a0)
    bsr     PrintString
    lea     MsgDebugPrompt(pc),a0
    bsr     PrintString
    ; Wait for Enter key
    bsr     WaitForEnter
    movem.l (sp)+,d0-d1/a0-a2
    ENDC

    ; Call the patch handler
    ; Handler returns D0: 0=skip, 1=success, -1=failure
    movem.l d1-d7/a1-a6,-(sp)
    jsr     (a0)
    movem.l (sp)+,d1-d7/a1-a6

    tst.l   d0
    beq     .PatchLoop          ; Skipped
    bmi     .PatchFailed        ; Failed

    ; Success - increment counter
    addq.l  #1,d6
    bra     .PatchLoop

.PatchFailed:
    ; Print failure message but continue
    lea     MsgPatchFail(pc),a0
    bsr     PrintString
    bra     .PatchLoop

.PatchDone:
    ; Print summary
    lea     MsgPatchCount(pc),a0
    bsr     PrintString
    move.l  d6,d0
    add.b   #'0',d0
    lea     TempChar(pc),a0
    move.b  d0,(a0)
    clr.b   1(a0)
    bsr     PrintString
    lea     MsgNewLine(pc),a0
    bsr     PrintString

    movem.l (sp)+,d0-d7/a0-a6
    rts

; =============================================================
; PATCH TABLE
; =============================================================
    EVEN
PatchTable:
    dc.l    PATCH_ID_BASE+PATCH_680X0
    dc.l    Patch_680x0Support

    dc.l    PATCH_ID_BASE+PATCH_AGA
    dc.l    Patch_AGAGraphics

    dc.l    PATCH_ID_BASE+PATCH_DCACHE
    dc.l    Patch_DataCache

    dc.l    0                   ; End marker

; =============================================================
; PATCH 0: Generic 680x0 Support Code
; Installs exception handlers and loads CPU library
; =============================================================
Patch_680x0Support:
    movem.l d1-d7/a0-a6,-(sp)

    move.l  DetectedCPU(pc),d7
    cmpi.l  #2,d7
    blt     .Skip_680x0         ; No support needed for 68000/68010

    lea     Msg680x0(pc),a0
    bsr     PrintString

    ; --- Step 1: Patch AttnFlags ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep1(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    move.l  AbsExecBase.w,a6
    jsr     _LVOForbid(a6)
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    bsr     ApplyAttnFlags
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)
    jsr     _LVOPermit(a6)

    ; --- Step 2: Fix vector 7 alignment ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep2(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    bsr     FixVector7Align
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    ; --- Step 3: Install exception handlers for 68060 ---
    cmpi.l  #6,d7
    bne.s   .Skip060Vectors

    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep3(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    lea     MsgInstallVec(pc),a0
    bsr     PrintString

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    bsr     Install060Vectors
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

.Skip060Vectors:
    ; --- Step 4: Probe CACR bit 14 ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep4(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    bsr     ProbeCacrBit14
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    ; --- Step 5: Save and disable caches for library load ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep5(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)
    moveq   #0,d5
    cmpi.l  #2,d7
    blt.s   .NoCacheSave
    dc.w    $4E7A,$5002         ; MOVEC CACR,D5
    moveq   #0,d0
    dc.w    $4E7B,$0002         ; MOVEC D0,CACR (Disable)
.NoCacheSave:
    lea     SavedCACR(pc),a0
    move.l  d5,(a0)
    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    ; --- Step 6: Load the CPU library ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep6(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    bsr     LoadCPULibrary
    move.l  d0,d6               ; Save result

    ; --- Step 7: Restore caches ---
    IFNE    DEBUG_PATCHES
    lea     MsgDbgStep7(pc),a0
    bsr     PrintString
    bsr     WaitForEnter
    ENDC

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)

    cmpi.l  #2,d7
    blt.s   .NoCacheRestore
    lea     SavedCACR(pc),a0
    move.l  (a0),d5
    dc.w    $4E7B,$5002         ; MOVEC D5,CACR
.NoCacheRestore:

    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    IFNE    DEBUG_PATCHES
    lea     MsgDbgDone(pc),a0
    bsr     PrintString
    ENDC

    ; Return success even if library failed (we installed vectors)
    movem.l (sp)+,d1-d7/a0-a6
    moveq   #1,d0
    rts

.Skip_680x0:
    movem.l (sp)+,d1-d7/a0-a6
    moveq   #0,d0               ; Skip, not needed
    rts

; =============================================================
; PATCH 1: Enable AGA Graphics Modes
; =============================================================
Patch_AGAGraphics:
    movem.l d1-d7/a0-a6,-(sp)

    ; Check for AGA chipset
    move.l  #LISAID,a0
    move.w  (a0),d0
    and.w   #$00FF,d0
    cmpi.w  #AGA_LISA_ID,d0
    bne     .NoAGA

    lea     MsgAGA(pc),a0
    bsr     PrintString

    ; Enable AGA modes via FMODE
    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)

    move.l  #FMODE,a0
    move.w  #$0003,(a0)         ; Enable 64-bit fetch mode

    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    movem.l (sp)+,d1-d7/a0-a6
    moveq   #1,d0
    rts

.NoAGA:
    movem.l (sp)+,d1-d7/a0-a6
    moveq   #0,d0               ; Skip, no AGA
    rts

; =============================================================
; PATCH 2: Enable Data Cache
; =============================================================
Patch_DataCache:
    movem.l d1-d7/a0-a6,-(sp)

    move.l  DetectedCPU(pc),d7
    cmpi.l  #2,d7
    blt     .NoCacheSupport     ; No cache on 68000/68010

    lea     MsgCache(pc),a0
    bsr     PrintString

    move.l  AbsExecBase.w,a6
    jsr     _LVOSuperState(a6)
    move.l  d0,-(sp)

    ; Read current CACR
    dc.w    $4E7A,$0002         ; MOVEC CACR,D0

    ; Enable data cache (and instruction cache)
    ; 68020/030: bit 0 = I-cache enable, bit 8 = D-cache enable
    ; 68040/060: different bit layout, but setting these is safe
    cmpi.l  #4,d7
    bge.s   .Cache040Plus

    ; 68020/030 cache enable
    or.l    #$0101,d0           ; Enable I-cache and D-cache
    bra.s   .SetCache

.Cache040Plus:
    ; 68040/060 - enable both caches
    ; For 040: bits 15,31 for I/D cache enable
    ; For 060: similar but also copyback bits
    cmpi.l  #6,d7
    beq.s   .Cache060

    ; 68040
    or.l    #$80008000,d0       ; Enable I and D caches
    bra.s   .SetCache

.Cache060:
    ; 68060 - enable caches with copyback
    or.l    #$A0C08000,d0       ; I-cache, D-cache, branch cache, store buffer

.SetCache:
    dc.w    $4E7B,$0002         ; MOVEC D0,CACR

    move.l  (sp)+,d0
    jsr     _LVOUserState(a6)

    movem.l (sp)+,d1-d7/a0-a6
    moveq   #1,d0
    rts

.NoCacheSupport:
    movem.l (sp)+,d1-d7/a0-a6
    moveq   #0,d0
    rts

; =============================================================
; INSTALL 68060 EXCEPTION VECTORS
; Must be called in supervisor mode
; =============================================================
Install060Vectors:
    movem.l d0-d2/a0-a2,-(sp)

    ; Get VBR
    dc.w    $4E7A,$8801         ; MOVEC VBR,A0

    ; --- Vector 11: F-Line Emulator ($2C) ---
    ; Save old vector
    move.l  VEC_FLINE(a0),d0
    lea     OldVec11(pc),a1
    move.l  d0,(a1)
    ; Install new handler
    lea     FLineHandler(pc),a1
    move.l  a1,VEC_FLINE(a0)
    ; Also install at absolute 0 for VBR=0 compatibility
    move.l  a1,VEC_FLINE.w

    ; --- Vector 60: Unimplemented FP ($F0) ---
    move.l  VEC_UNIMP_FP(a0),d0
    lea     OldVec60(pc),a1
    move.l  d0,(a1)
    lea     UnimpFPHandler(pc),a1
    move.l  a1,VEC_UNIMP_FP(a0)
    move.l  a1,VEC_UNIMP_FP.w

    ; --- Vector 61: Unimplemented Integer ($F4) ---
    move.l  VEC_UNIMP_INT(a0),d0
    lea     OldVec61(pc),a1
    move.l  d0,(a1)
    lea     UnimpIntHandler(pc),a1
    move.l  a1,VEC_UNIMP_INT(a0)
    move.l  a1,VEC_UNIMP_INT.w

    ; Clear caches (CacheClearU requires exec V37+)
    move.l  AbsExecBase.w,a6
    cmpi.w  #37,LIB_VERSION(a6)
    blt.s   .SkipCacheClr1
    jsr     _LVOCacheClearU(a6)
.SkipCacheClr1:

    movem.l (sp)+,d0-d2/a0-a2
    rts

; =============================================================
; EXCEPTION HANDLERS
; These are minimal handlers - the 68060.library provides better ones
; =============================================================

; --- F-Line Handler (Vector 11) ---
; Handles F-line (coprocessor) instructions
; 68060 uses this for FPU instructions when FPU is disabled
    EVEN
FLineHandler:
    ; Check if there's an old handler to chain to
    move.l  OldVec11(pc),d0
    beq.s   .FLine_NoChain

    ; Chain to old handler
    move.l  d0,-(sp)
    rts                         ; Jump to old handler

.FLine_NoChain:
    ; No old handler - try to handle or crash gracefully
    ; For now, just skip the instruction (dangerous but prevents hang)
    ; A real implementation would emulate the FPU instruction

    ; Check if from supervisor mode
    btst    #5,(sp)
    bne.s   .FLine_Super

    ; User mode - add 4 to stacked PC (skip the F-line instruction)
    addq.l  #4,2(sp)
    rte

.FLine_Super:
    ; Supervisor mode - 6-byte stack frame on 68060
    addq.l  #4,2(sp)
    rte

; --- Unimplemented FP Handler (Vector 60) ---
; Handles unimplemented FPU operations on 68060
    EVEN
UnimpFPHandler:
    move.l  OldVec60(pc),d0
    beq.s   .UFP_NoChain

    move.l  d0,-(sp)
    rts

.UFP_NoChain:
    ; No emulation - skip instruction
    ; Real 68060.library would emulate transcendental functions etc.
    btst    #5,(sp)
    bne.s   .UFP_Super
    addq.l  #4,2(sp)
    rte
.UFP_Super:
    addq.l  #4,2(sp)
    rte

; --- Unimplemented Integer Handler (Vector 61) ---
; Handles MOVEP, CAS, CAS2, 64-bit MUL/DIV on 68060
    EVEN
UnimpIntHandler:
    move.l  OldVec61(pc),d0
    beq.s   .UInt_NoChain

    move.l  d0,-(sp)
    rts

.UInt_NoChain:
    ; No emulation available - this is problematic
    ; Real 68060.library emulates MOVEP, CAS, CAS2, etc.
    ; For now, skip (this WILL break code using these instructions)
    btst    #5,(sp)
    bne.s   .UInt_Super
    addq.l  #4,2(sp)
    rte
.UInt_Super:
    addq.l  #4,2(sp)
    rte

; =============================================================
; CPU DETECTION
; Must be called in supervisor mode
; =============================================================
DetectCPU:
    movem.l d1-d4/a0-a2,-(sp)

    ; Save Vector 4 (Illegal Instruction)
    move.l  $10.w,d2
    lea     .Trap(pc),a0
    move.l  a0,$10.w

    ; Check 68010+ (VBR exists)
    moveq   #0,d0
    dc.w    $4E7A,$0801         ; MOVEC VBR,D0

    tst.l   d0
    beq.s   .VBR_Zero

    ; Patch trap at VBR+$10
    move.l  d0,a1
    move.l  $10(a1),d3
    move.l  a0,$10(a1)

.VBR_Zero:
    moveq   #1,d0               ; At least 68010

    ; Check 68020+ (CACR)
    dc.w    $4E7A,$1002         ; MOVEC CACR,D1
    moveq   #2,d0

    ; Assume 68030 if 68020 passes (simplified)
    moveq   #3,d0

    ; Check 68040/060 (ITT0)
    dc.w    $4E7A,$1004         ; MOVEC ITT0,D1
    moveq   #4,d0

    ; Check 68060 (PCR)
    dc.w    $4E7A,$1808         ; MOVEC PCR,D1
    moveq   #6,d0

.Cleanup:
    move.l  d2,$10.w            ; Restore absolute vector

    tst.l   d0
    beq.s   .EndDetect

    dc.w    $4E7A,$1801         ; MOVEC VBR,D1
    tst.l   d1
    beq.s   .EndDetect

    move.l  d1,a1
    move.l  d3,$10(a1)          ; Restore VBR vector

.EndDetect:
    movem.l (sp)+,d1-d4/a0-a2
    rts

.Trap:
    tst.l   d0
    bne.s   .Frame8
    addq.l  #6,sp
    bra.s   .Cleanup
.Frame8:
    addq.l  #8,sp
    bra.s   .Cleanup

; =============================================================
; APPLY ATTNFLAGS
; Must be called in supervisor mode
; =============================================================
ApplyAttnFlags:
    move.l  AbsExecBase.w,a6
    move.l  DetectedCPU(pc),d7

    move.w  EB_AttnFlags(a6),d1

    ; Clear CPU bits but keep FPU bits
    and.w   #$FF70,d1

    cmpi.l  #6,d7
    beq.s   .Set060
    cmpi.l  #4,d7
    beq.s   .Set040
    cmpi.l  #3,d7
    beq.s   .Set030
    cmpi.l  #2,d7
    beq.s   .Set020
    cmpi.l  #1,d7
    beq.s   .Set010
    bra.s   .CommitFlags

.Set060:
    bset    #AFB_68060,d1
    bset    #AFB_68040,d1       ; 060 is also 040 compatible for AttnFlags
    bset    #AFB_68030,d1
    bset    #AFB_68020,d1
    bset    #AFB_68010,d1
    bra.s   .CommitFlags

.Set040:
    bset    #AFB_68040,d1
    bset    #AFB_FPU40,d1
    bset    #AFB_68030,d1
    bset    #AFB_68020,d1
    bset    #AFB_68010,d1
    bra.s   .CommitFlags

.Set030:
    bset    #AFB_68030,d1
    bset    #AFB_68020,d1
    bset    #AFB_68010,d1
    bra.s   .CommitFlags

.Set020:
    bset    #AFB_68020,d1
    bset    #AFB_68010,d1
    bra.s   .CommitFlags

.Set010:
    bset    #AFB_68010,d1

.CommitFlags:
    move.w  d1,EB_AttnFlags(a6)
    rts

; =============================================================
; FIX VECTOR 7 ALIGNMENT
; Must be called in supervisor mode
; =============================================================
FixVector7Align:
    movem.l d0/a0/a6,-(sp)
    dc.w    $4E7A,$8801         ; MOVEC VBR,A0
    moveq   #-2,d0
    and.l   d0,28(a0)           ; Clear bit 0 of vector 7
    ; CacheClearU requires exec V37+
    move.l  AbsExecBase.w,a6
    cmpi.w  #37,LIB_VERSION(a6)
    blt.s   .SkipCacheClr2
    jsr     _LVOCacheClearU(a6)
.SkipCacheClr2:
    movem.l (sp)+,d0/a0/a6
    rts

; =============================================================
; PROBE CACR BIT 14
; Must be called in supervisor mode
; Returns D0=1 if bit 14 can be toggled
; =============================================================
ProbeCacrBit14:
    movem.l d1-d3,-(sp)
    dc.w    $4E7A,$1002         ; MOVEC CACR,D1
    move.l  d1,d2
    bchg    #14,d2
    dc.w    $4E7B,$2002         ; MOVEC D2,CACR
    dc.w    $4E7A,$3002         ; MOVEC CACR,D3
    dc.w    $4E7B,$1002         ; MOVEC D1,CACR (restore)
    eor.l   d1,d3
    btst    #14,d3
    sne     d0
    ext.w   d0
    ext.l   d0
    movem.l (sp)+,d1-d3
    rts

; =============================================================
; LOAD CPU LIBRARY
; =============================================================
LoadCPULibrary:
    movem.l d1-d7/a0-a6,-(sp)

    move.l  DetectedCPU(pc),d7

    ; Build library name
    lea     LibBuffer(pc),a1
    lea     LibTemplate(pc),a0
    move.b  (a0)+,(a1)+         ; '6'
    move.b  (a0)+,(a1)+         ; '8'
    move.b  (a0)+,(a1)+         ; '0'

    move.b  #'0',d0
    cmpi.l  #6,d7
    bne.s   .try40
    move.b  #'6',d0
    bra.s   .write_digit
.try40:
    cmpi.l  #4,d7
    bne.s   .try30
    move.b  #'4',d0
    bra.s   .write_digit
.try30:
    cmpi.l  #3,d7
    bne.s   .try20
    move.b  #'3',d0
    bra.s   .write_digit
.try20:
    cmpi.l  #2,d7
    bne.s   .write_digit
    move.b  #'2',d0
.write_digit:
    move.b  d0,(a1)+
    move.b  #'0',(a1)+

    lea     LibSuffix(pc),a0
.copy_suffix:
    move.b  (a0)+,(a1)+
    bne.s   .copy_suffix

    ; Print what we're loading
    lea     MsgLoading(pc),a0
    bsr     PrintString
    lea     LibBuffer(pc),a0
    bsr     PrintString
    lea     MsgEllipsis(pc),a0
    bsr     PrintString

    ; Try to open the library
    move.l  AbsExecBase.w,a6
    lea     LibBuffer(pc),a1
    moveq   #0,d0
    jsr     _LVOOpenLibrary(a6)

    tst.l   d0
    bne.s   .LibOK

    ; Library failed - get error code
    move.l  d0,-(sp)            ; Save result (0)

    move.l  DOSBase(pc),a6
    jsr     _LVOIoErr(a6)

    ; Print error info
    lea     MsgFailed(pc),a0
    bsr     PrintString

    ; Format and print IoErr
    move.l  d0,d1
    lea     MsgIoErr(pc),a0
    bsr     PrintString
    bsr     PrintHexLong        ; Print D1 as hex
    lea     MsgNewLine(pc),a0
    bsr     PrintString

    move.l  (sp)+,d0            ; Restore result (0)
    bra.s   .LibDone

.LibOK:
    lea     LibBase(pc),a0
    move.l  d0,(a0)             ; Save library base

    lea     MsgOK(pc),a0
    bsr     PrintString
    moveq   #1,d0

.LibDone:
    movem.l (sp)+,d1-d7/a0-a6
    rts

; =============================================================
; PRINT HELPERS
; =============================================================
PrintString:
    ; Check quiet mode first (use LEA+TST as TST doesn't support d(PC) on 68000)
    ; A1 is scratch here - saved by movem.l below if we continue
    lea     QuietMode(pc),a1
    tst.b   (a1)
    bne.s   .p_quiet
    movem.l d0-d3/a0-a1/a6,-(sp)
    move.l  a0,a1
    move.l  DOSBase(pc),d0
    beq.s   .p_exit
    move.l  d0,a6
    jsr     _LVOOutput(a6)
    tst.l   d0
    beq.s   .p_exit
    move.l  d0,d1
    move.l  a1,d2
    move.l  a1,a0
.strlen:
    tst.b   (a0)+
    bne.s   .strlen
    subq.l  #1,a0
    sub.l   d2,a0
    move.l  a0,d3
    jsr     _LVOWrite(a6)
.p_exit:
    movem.l (sp)+,d0-d3/a0-a1/a6
.p_quiet:
    rts

PrintCPUFound:
    lea     MsgFoundCPU(pc),a0
    bsr     PrintString
    move.l  DetectedCPU(pc),d7
    lea     Str68000(pc),a0
    tst.l   d7
    beq.s   .p_cpu
    lea     Str68010(pc),a0
    cmpi.l  #1,d7
    beq.s   .p_cpu
    lea     Str68020(pc),a0
    cmpi.l  #2,d7
    beq.s   .p_cpu
    lea     Str68030(pc),a0
    cmpi.l  #3,d7
    beq.s   .p_cpu
    lea     Str68040(pc),a0
    cmpi.l  #4,d7
    beq.s   .p_cpu
    lea     Str68060(pc),a0
.p_cpu:
    bsr     PrintString
    lea     MsgNewLine(pc),a0
    bra     PrintString

    IFNE    DEBUG_PATCHES
; Wait for Enter key from user
WaitForEnter:
    movem.l d0-d3/a0-a1/a6,-(sp)
    move.l  DOSBase(pc),a6
    jsr     _LVOInput(a6)
    tst.l   d0
    beq.s   .wait_exit
    move.l  d0,d1               ; File handle
    lea     TempChar(pc),a0
    move.l  a0,d2               ; Buffer
    moveq   #1,d3               ; Read 1 byte
    jsr     _LVORead(a6)
.wait_exit:
    movem.l (sp)+,d0-d3/a0-a1/a6
    rts
    ENDC

; Print D1 as 8-digit hex
PrintHexLong:
    movem.l d0-d2/a0-a1,-(sp)
    lea     HexBuffer(pc),a0
    move.l  d1,d0
    moveq   #7,d2
.hex_loop:
    rol.l   #4,d0
    move.l  d0,d1
    and.l   #$0F,d1
    cmpi.b  #10,d1
    blt.s   .hex_digit
    add.b   #'A'-10,d1
    bra.s   .hex_store
.hex_digit:
    add.b   #'0',d1
.hex_store:
    move.b  d1,(a0)+
    dbf     d2,.hex_loop
    clr.b   (a0)
    lea     HexBuffer(pc),a0
    bsr     PrintString
    movem.l (sp)+,d0-d2/a0-a1
    rts

; =============================================================
; PARSE COMMAND LINE ARGUMENTS
; Checks for QUIET keyword (case insensitive)
; Uses saved D0/A0 from program entry (works on all Kickstart versions)
; =============================================================
ParseArgs:
    movem.l d0-d2/a0-a2,-(sp)

    ; Get saved command line (D0=length, A0=pointer from program entry)
    move.l  ArgLength(pc),d0
    beq.s   .pa_done                ; No arguments
    move.l  ArgPointer(pc),a0       ; A0 = argument string

    ; Scan for "QUIET" keyword
    lea     ArgQuiet(pc),a1         ; A1 = "QUIET" to match
.pa_scan:
    move.b  (a0)+,d0
    beq.s   .pa_done                ; End of args, not found

    ; Skip whitespace
    cmpi.b  #' ',d0
    beq.s   .pa_scan
    cmpi.b  #9,d0                   ; Tab
    beq.s   .pa_scan
    cmpi.b  #10,d0                  ; Newline
    beq.s   .pa_scan

    ; Try to match "QUIET"
    subq.l  #1,a0                   ; Back up to first char
    lea     ArgQuiet(pc),a1
.pa_match:
    move.b  (a1)+,d1
    beq.s   .pa_found               ; Matched all of "QUIET"
    move.b  (a0)+,d0
    beq.s   .pa_done                ; End of string, no match

    ; Case insensitive compare
    cmpi.b  #'a',d0
    blt.s   .pa_noupper
    cmpi.b  #'z',d0
    bgt.s   .pa_noupper
    subi.b  #$20,d0                 ; Convert to uppercase
.pa_noupper:
    cmp.b   d1,d0
    beq.s   .pa_match               ; Continue matching

    ; No match, skip to next word
.pa_skipword:
    move.b  (a0)+,d0
    beq.s   .pa_done
    cmpi.b  #' ',d0
    beq.s   .pa_scan
    cmpi.b  #9,d0
    beq.s   .pa_scan
    cmpi.b  #10,d0
    beq.s   .pa_scan
    bra.s   .pa_skipword

.pa_found:
    ; Set quiet mode flag
    lea     QuietMode(pc),a0
    move.b  #1,(a0)

.pa_done:
    movem.l (sp)+,d0-d2/a0-a2
    rts

; =============================================================
; DATA SECTION
; =============================================================
    EVEN
DOSBase:        dc.l    0
LibBase:        dc.l    0
DetectedCPU:    dc.l    0
SavedCACR:      dc.l    0
ArgLength:      dc.l    0           ; Saved from D0 at entry
ArgPointer:     dc.l    0           ; Saved from A0 at entry
QuietMode:      dc.b    0           ; Non-zero = suppress output
    EVEN

; Saved exception vectors
OldVec11:       dc.l    0
OldVec60:       dc.l    0
OldVec61:       dc.l    0

; SetPatch semaphore - this is what 68060.library looks for
; Name format: « SetPatch » (with guillemets)
SemaphoreName:  dc.b    $AB," SetPatch ",$BB,0
    EVEN
; Pointer to allocated semaphore structure
SetPatchSemaPtr: dc.l   0

; Strings
DOSName:        dc.b    "dos.library",0
LibTemplate:    dc.b    "680",0
LibSuffix:      dc.b    ".library",0
ArgQuiet:       dc.b    "QUIET",0       ; Command line argument
    EVEN
LibBuffer:      ds.b    32

MsgStart:       dc.b    "TinySetPatch v0.1",10,0
MsgAlready:     dc.b    "TinySetPatch already loaded. Exiting.",10,0
MsgDetect:      dc.b    "Probing hardware... ",0
MsgFoundCPU:    dc.b    "Detected: ",0
MsgProcessing:  dc.b    "Installing magic...",10,0
MsgPatchFail:   dc.b    "  Patch failed!",10,0
MsgPatchCount:  dc.b    "Patches applied: ",0
Msg680x0:       dc.b    "  680x0 Support: ",0
MsgInstallVec:  dc.b    "Installing exception vectors...",10,0
MsgAGA:         dc.b    "  AGA Graphics: Enabling 64-bit fetch",10,0
MsgCache:       dc.b    "  Data Cache: Enabling",10,0
MsgLoading:     dc.b    "Loading ",0
MsgEllipsis:    dc.b    "... ",0
MsgOK:          dc.b    "OK.",10,0
MsgFailed:      dc.b    "FAILED",10,0
MsgIoErr:       dc.b    "  IoErr: $",0
MsgDone:        dc.b    "Initialization complete.",10,0
MsgNewLine:     dc.b    10,0

    IFNE    DEBUG_PATCHES
MsgDebugNext:   dc.b    ">> Next patch: ",0
MsgDebugPrompt: dc.b    " - Press Enter to run...",10,0
MsgDbgStep1:    dc.b    "  [1/7] ApplyAttnFlags - Enter:",0
MsgDbgStep2:    dc.b    "  [2/7] FixVector7Align - Enter:",0
MsgDbgStep3:    dc.b    "  [3/7] Install060Vectors - Enter:",0
MsgDbgStep4:    dc.b    "  [4/7] ProbeCacrBit14 - Enter:",0
MsgDbgStep5:    dc.b    "  [5/7] DisableCaches - Enter:",0
MsgDbgStep6:    dc.b    "  [6/7] LoadCPULibrary - Enter:",0
MsgDbgStep7:    dc.b    "  [7/7] RestoreCaches - Enter:",0
MsgDbgDone:     dc.b    "  Patch 0 complete!",10,0
    ENDC

Str68000:       dc.b    "68000",0
Str68010:       dc.b    "68010",0
Str68020:       dc.b    "68020",0
Str68030:       dc.b    "68030",0
Str68040:       dc.b    "68040",0
Str68060:       dc.b    "68060",0

    EVEN
TempChar:       ds.b    4
HexBuffer:      ds.b    12

; =============================================================
; VERSION STRING
; =============================================================
Version:        dc.b    "$VER: TinySetPatch 0.1 (17.12.2025)",0
    EVEN
    END
