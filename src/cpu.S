; SPDX-License-Identifier: BSD-2-Clause
; SPDX-FileCopyrightText: 2025 copyright by Matthias Heinrichs (CPUReg, RamseyRev/Ctrl, glue-code) , Henryk Richter (68060-detection), Thomas Richter (MMU) and Christian Euler (VBR-detection)
	INCDIR  "include:"
	INCLUDE "lvo/exec_lib.i"

BusErrorVector			EQU $8
IllegalInstructionVector	EQU $10
Line1111Vector			EQU $2C
AttnFlags		EQU   296

_LVOSuperVisor		EQU   -30


CACRB_EnableI 			EQU $1		;Enable instruction cache bit 020/30
CACRF_EnableI			EQU $2


ASM_CPU_68000 EQU 1
ASM_CPU_68010 EQU 2
ASM_CPU_68020 EQU 3
ASM_CPU_68EC020 EQU 4
ASM_CPU_68030 EQU 5
ASM_CPU_68EC030 EQU 6
ASM_CPU_68040 EQU 7
ASM_CPU_68LC040 EQU 8
ASM_CPU_68EC040 EQU 9
ASM_CPU_68060 EQU 10
ASM_CPU_68EC060 EQU 11
ASM_CPU_68LC060 EQU 12
ASM_CPU_UNKNOWN  EQU 13
ASM_MMU EQU 1
RAMSEY_VER EQU $DE0043
RAMSEY_CTRL EQU $DE0003


    SECTION Code,CODE
    MACHINE 68060
	XDEF	_GetCPUReg
	XDEF	_GetCPU060;
	XDEF	_GetVBR
	XDEF	_GetMMU
	XDEF	_GetRamseyRev
	XDEF	_GetRamseyCtrl

_GetCPUReg:
	movem.l	a5/a6,-(sp)
	lea.l	_readreg,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,a5/a6
	rts

_readreg:
	movec	PCR,d0
	rte

_GetCPU060:
	movem.l	d1/a0/a1/a2/a5/a6,-(sp)
	lea.l	_read060fpu,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,d1/a0/a1/a2/a5/a6
	rts

_read060fpu
	move.l	IllegalInstructionVector.w,a1	;save Illegal Instruction and LineF vectors
	move.l	Line1111Vector.w,a2
	lea	crashvector(pc),a0
	move.l	a0,IllegalInstructionVector.w
	move.l	a0,Line1111Vector.w
	move.l	sp,a0

	;the code below avoids a crash on xx060
	;alternatively, just the moveq/movec would suffice where all other CPUs
	;(20/30,xx040,LC060,EC060) are pushed into the trap vector trampoline

	;test for 040/60
	movec	CACR,d1					; this will crash on 68000/010 -> crashvector
	move.l	d1,d0					; copy
	or.l	#CACRF_EnableI,d0		; 020/30 only flag
	movec	d0,CACR					; this will crash on 68000/010 -> crashvector
	movec	CACR,d0
	movec	d1,CACR					; restore old cache state
	btst	#CACRB_EnableI,d0		; this flag is ignored on 68040/60
	bne.s	no_040_60


	movec	PCR,d0				; this will crash 040 -> crashvector
	swap	d0				; get upper 16 Bit
	cmp.w	#$430,d0			; ID for full 060 (EC/LC have 0x431)
	bne.s	no_060_FPU			; nope, no need for FP disable
	moveq	#ASM_CPU_68060,d0
	bra.s	vectorrestore			;we have a 68060 with FPU
no_060_FPU:
	moveq	#ASM_CPU_68LC060,d0
	bra.s	vectorrestore			;we have a 68LC/EC060 without FPU
no_040_60:
	moveq	#ASM_CPU_68020,d0
	bra.s	vectorrestore		;we have a 68020/030-CPU
crashvector:
	moveq	#ASM_CPU_68040,d0
vectorrestore:
	move.l	a0,sp				;restore Stack Pointer
	move.l	a1,IllegalInstructionVector.w	;restore Illegal Instruction and LineF vectors
	move.l	a2,Line1111Vector.w
	rte

_GetVBR:
	movem.l	a5/a6,-(sp)
	lea.l	_readvbr,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,a5/a6
	rts

_readvbr:
	movec	VBR,d0
	rte

_GetMMU:	;d0 holds the cpu type: either ASM_CPU_68020, ASM_CPU_68030 or ASM_CPU_68040
	movem.l	a5/a6,-(sp)
	lea.l	_testMMU,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,a5/a6
	tst.l	d0 ; set condition codes
	rts

_testMMU
	cmp.l	#ASM_CPU_68020,d0		;is it 68020-code?
	beq		m68020_mmu
	cmp.l	#ASM_CPU_68030,d0		;is it 68030-code? ->do 68020 too!
	beq		m68030_mmu
	cmp.l	#ASM_CPU_68040,d0		;is it 68040-code?
	beq		m68040_mmu
	cmp.l	#ASM_CPU_68060,d0		;is it 68060-code?
	beq		m68040_mmu
	moveq #0,d0						;not supported cpu!
	rte
m68020_mmu:
	MACHINE 68020
	MACHINE 68851
        ori.w #$0700,sr                 ;disable interrupts
        movem.l	 d2-d5/a2-a3,-(sp)
        lea -$f0(a7),a7					;reserve room for termporary
                                        ;exception vectors
		;structure as follows:
        ;defvar
        ; auto.b ec_autovectors,$f0      ;for autovectors (huh!) 0-3B in long
        ; auto.l ec_tt0					;3C
        ; auto.l ec_tt1					;3D
        ; auto.l ec_tc					;3E
        ; auto.l ec_urp,2				;3F,40
        ; auto.l ec_zero				;41
        ; auto.l ec_newurp,2			;42,43
        ; auto.l ec_newtc				;44
        ; auto.b ec_newroot,$20         ;45-4C root pointer with early termination
        ; auto.b ec_round,$10+4			;4D-52
        ;endvar

        moveq #ASM_MMU,d0               ;default result is: YES
        movec.l vbr,a0                  ;saveback VBR
        lea .illegal(pc),a1             ;entry for Illegal/LineF
        move.l a1,$10(a7)               ;set it
        move.l a1,$2c(a7)               ;ditto
        move.l a1,$34(a7)
        move.l a1,$e0(a7)
        move.l a1,$e4(a7)
        move.l a1,$e8(a7)
        lea $45(a7),a3           		;place here the new root pointer (temporarely)
        move.l a3,d2
        addq.l #$8,d2
        addq.l #$7,d2
        and.b #$f0,d2                   ;round to $10
        move.l d2,a3                    ;new root pointer (dummy)
        lea .nmi(pc),a1
        move.l a1,$7c(a7)               ;NMI disable
        movec.l a7,vbr                  ;VBR temporary on stack
        movec.l cacr,d2
        or.w #$0808,d2                  ;prepare for cache flush
        lea .continue(pc),a1            ;jump address in case of fault
        clr.l $41(a7)               	;dummy for read


        pflusha
        pmove.l tc,$3e(a7)            	;try to read translation control
        movec.l d2,cacr                 ;flush the cache
        pmove.l $41(a7),tc         		;try to write it
        pflusha                         ;this might generate an exception for the EC30

        ;it's a 020 with a 68851, we're done testing!
        tst.l $3e(a7)                 	;Enabled ?
        bpl.s .continue                 ;continue if disabled
        pmove.d $3f(a7),crp          	;restore the urp
        pmove.l $3e(a7),tc            	;is enabled. If so, must be valid
        movec.l d2,cacr                 ;flush the cache
        pflusha
.continue:

        movec.l a0,vbr                  ;restore VBR
                                        ;result is in d0
        lea $f0(a7),a7                  ;release stack
        movem.l	(sp)+,d2-d5/a2-a3
.nmi:
        rte                             ;this restores interrupts anyways
.illegal:
        moveq #0,d0
        move.l a1,2(a7)
        rte

m68030_mmu:
	;first tests go with 68851-code which generate some exceptions on a "real" 68EC030
	MACHINE 68020
	MACHINE 68851
        ori.w #$0700,sr                 ;disable interrupts
        movem.l	 d2-d5/a2-a3,-(sp)
        lea -$f0(a7),a7					;reserve room for termporary
                                        ;exception vectors
		;structure as follows:
        ;defvar
        ; auto.b ec_autovectors,$f0      ;for autovectors (huh!) 0-3B in long
        ; auto.l ec_tt0					;3C
        ; auto.l ec_tt1					;3D
        ; auto.l ec_tc					;3E
        ; auto.l ec_urp,2				;3F,40
        ; auto.l ec_zero				;41
        ; auto.l ec_newurp,2			;42,43
        ; auto.l ec_newtc				;44
        ; auto.b ec_newroot,$20         ;45-4C root pointer with early termination
        ; auto.b ec_round,$10+4			;4D-52
        ;endvar

        moveq #ASM_MMU,d0               ;default result is: YES
        movec.l vbr,a0                  ;saveback VBR
        lea .illegal(pc),a1             ;entry for Illegal/LineF
        move.l a1,$10(a7)               ;set it
        move.l a1,$2c(a7)               ;ditto
        move.l a1,$34(a7)
        move.l a1,$e0(a7)
        move.l a1,$e4(a7)
        move.l a1,$e8(a7)
        lea $45(a7),a3           		;place here the new root pointer (temporarely)
        move.l a3,d2
        addq.l #$8,d2
        addq.l #$7,d2
        and.b #$f0,d2                   ;round to $10
        move.l d2,a3                    ;new root pointer (dummy)
        lea .nmi(pc),a1
        move.l a1,$7c(a7)               ;NMI disable
        movec.l a7,vbr                  ;VBR temporary on stack
        movec.l cacr,d2
        or.w #$0808,d2                  ;prepare for cache flush
        lea .continue(pc),a1            ;jump address in case of fault
        clr.l $41(a7)               	;dummy for read


        pflusha
        pmove.l tc,$3e(a7)            	;try to read translation control
        movec.l d2,cacr                 ;flush the cache
        pmove.l $41(a7),tc         		;try to write it
        pflusha                         ;this might generate an exception for the EC30

        ;ok mmu-codes do not produce an error, try enabling it!
	MACHINE 68030
        pmove.l tt0,$3c(a7)
        pmove.l tt1,$3d(a7)
        pmove.d crp,$3f(a7)          ;saveback registers

        pmove.l $41(a7),tt0         ;disable tt0
        pmove.l $41(a7),tt1         ;disable tt1
        lea $f80000,a2                          ;ROMStart
        moveq #(1<<6)!(1<<4)!(1<<3)!1,d5        ;early termination, CI, U, M


        move.l 4(a2),d3                 ;read second address, offset $f80000 (ROM BOOT address)
        move.l a2,d2                    ;displaced page descriptor
        or.l d5,d2                      ;set type bits: page descriptor for displaced access

        move.l #$80000002,$42(a7) ;write a page descriptor into the urp
        move.l a3,4+$42(a7)       ;write dummy table

        move.l d5,$14(a3)               ;supervisor data space          (undisplaced)
        move.l d5,$18(a3)               ;supervisor program space       (undisplaced)
        move.l d2,$4(a3)                ;user data space                (displaced)
        move.l d5,$8(a3)                ;user program space             (undisplaced)

        move.l #$81f09800,$44(a7)  ;write new translation control: page size 32K, 9 entries level A, 8 entries level B, FCL!
        pflusha
        moveq #1,d2                     ;user data space
        pmove.d $42(a7),crp       ;load modified urp
        movec.l d2,sfc
        pmove.l $44(a7),tc         ;enable the stuff
        pflusha
        movec.l cacr,d2
        sub.l a3,a3                     ;get address
        or.w #$808,d2
        movec.l d2,cacr                 ;clear the cache


        moves.l 4(a3),d4                ;read either AbsExecBase or ROMBoot (Can't be equal) from user data space

        pflusha
        pmove.l $41(a7),tc          ;disable the MMU again
        pflusha
        movec.l d2,cacr                 ;flush the cache again
        tst.w 2+$3c(a7)              ;re-enable tt0 ?
        bpl.s .nott0
        pmove.l $3c(a7),tt0
.nott0:
        tst.w 2+$3d(a7)
        bpl.s .nott1
        pmove.l $3d(a7),tt1
.nott1:
        movec.l d2,cacr

        cmp.l d4,d3                     ;ROMBoot must have been available at AbsExecBase
        beq.s .fine
        moveq #0,d0
.fine:
        tst.l $3e(a7)                 	;Enabled ?
        bpl.s .continue                 ;continue if disabled
        pmove.d $3f(a7),crp          	;restore the urp
        pmove.l $3e(a7),tc            	;is enabled. If so, must be valid
        movec.l d2,cacr                 ;flush the cache
        pflusha
.continue:

        movec.l a0,vbr                  ;restore VBR
                                        ;result is in d0
        lea $f0(a7),a7                  ;release stack
        movem.l	(sp)+,d2-d5/a2-a3
.nmi:
        rte                             ;this restores interrupts anyways
.illegal:
        moveq #0,d0
        move.l a1,2(a7)
        rte


m68040_mmu:
;FOLD Check040MMU
;*************************************************
;** Check040MMU                                 **
;** Check, whether a 68040 MMU is on board      **
;*************************************************
        MACHINE mc68040
        ori.w #$0700,sr                 ;disable interrupts
        lea -$80(a7),a7                 ;reserve room for termporary
                                        ;exception vectors
        movec.l vbr,a0                  ;saveback VBR
        lea .illegal(pc),a1             ;entry for Illegal/LineF
        move.l a1,$10(a7)               ;set it
        move.l a1,$2c(a7)               ;ditto
        lea .nmi(pc),a1
        move.l a1,$7c(a7)               ;NMI disable
        movec.l a7,vbr                  ;VBR temporary on stack
        moveq #ASM_MMU,d0                     ;default result is: YES
        moveq #0,d1
        sub.l a1,a1
        movec.l tc,d1                   ;try to read translation control
        pflusha                         ;this generates an exception for the EC40
        movec.l a1,tc                   ;try to clear it
        tst.w d1                        ;enabled ?
        bpl.s .continue
        movec.l d1,tc                   ;if enabled, write back
.continue:
        movec.l a0,vbr                  ;restore VBR
                                        ;result is in d0
        lea $80(a7),a7                  ;release stack
.nmi:
        rte                             ;this restores interrupts anyways
.illegal:
        moveq #0,d0
        addq.l #8,a7                    ;remove exception stack frame
        bra.s .continue
;ENDFOLD

_GetRamseyRev:
	movem.l	a0/a5/a6,-(sp)
	lea.l	_read_ramsey_rev,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,a0/a5/a6
	rts

_read_ramsey_rev:
	lea	RAMSEY_VER,a0
	move.b	(a0),d0
	rte

_GetRamseyCtrl:
	movem.l	a0/a5/a6,-(sp)
	lea.l	_read_ramsey_ctrl,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,a0/a5/a6
	rts

_read_ramsey_ctrl:
	lea	RAMSEY_CTRL,a0
	move.b	(a0),d0
	rte



    END
