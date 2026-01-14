;copyright by Matthias Heinrichs, Henryk Richter (68060-detection) and Christian Euler (VBR-detection)
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


    SECTION Code,CODE
    MACHINE 68060
	XDEF	_GetCPUReg
	XDEF	_GetCPU060;
	XDEF	_GetVBR
	XDEF	_GetMMU

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

_GetMMU:	;d0 holds the cpu type: either ASM_CPU_68020, ASM_CPU_68030 or ASM_CPU_68040
	movem.l	d1/d2/d3/a0/a1/a2/a5/a6,-(sp)
	lea.l	_testMMU,a5
	move.l	$4.w,a6
	jsr	_LVOSupervisor(a6)
	movem.l	(sp)+,d1/d2/d3/a0/a1/a2/a5/a6
	tst.l	d0 ; set condition codes
	rts

_testMMU
	move.l	IllegalInstructionVector.w,a1	;save Illegal Instruction and LineF vectors
	move.l	Line1111Vector.w,a2
	lea	crashvectorMMU(pc),a0
	move.l	a0,IllegalInstructionVector.w
	move.l	a0,Line1111Vector.w

    move.l	d0,-(sp)				;push a variable on stack (to have a save address for later)

    movec.l cacr,d2					;read cache
    move.l d2,d1					;copy cache-state
    or.w #$0808,d2                  ;prepare for cache flush

	cmp.l	#ASM_CPU_68020,d0		;is it 68020-code?
	beq.s	m68020_mmu
	cmp.l	#ASM_CPU_68020,d0		;is it 68030-code? ->do 68020 too!
	beq.s	m68030_mmu
	cmp.l	#ASM_CPU_68040,d0		;is it 68040-code?
	beq.s	m68040_mmu
	cmp.l	#ASM_CPU_68040,d0		;is it 68060-code?
	beq.s	m68060_mmu
	bra.s	crashvectorMMU			;not supported detection!
m68020_mmu:
	MACHINE 68020
	MACHINE 68851
    pflusha
    pmove.l tc,(sp)            		;try to read translation control
    movec.l d2,cacr                 ;flush the cache
    pmove.l (sp),tc          ;try to write it
    pflusha                         ;this might generate an exception for the EC30
    MACHINE 68060
    bra.s mmu_test_done
m68030_mmu:
	MACHINE 68020
	MACHINE 68851
    pflusha							;this might generate an exception for the EC30
    pmove.l tc,(sp)            		;try to read translation control
    movec.l d2,cacr                 ;flush the cache
    pmove.l (sp),tc          ;try to write it
    pflusha                         ;this might generate an exception for the EC30
    MACHINE 68060
    bra.s mmu_test_done
m68040_mmu:
    MACHINE 68040
	pflusha
    movec.l tc,a0            		;try to read translation control
    movec.l d2,cacr                 ;flush the cache
    movec.l a0,tc          			;try to write it
    pflusha                         ;this might generate an exception for the EC40/EC60
    MACHINE 68060
    bra.s mmu_test_done
m68060_mmu:
    MACHINE 68060
	pflusha
    movec.l tc,a0            		;try to read translation control
    movec.l d2,cacr                 ;flush the cache
    movec.l a0,tc          			;try to write it
    pflusha                         ;this might generate an exception for the EC40/EC60
mmu_test_done:
	moveq	#ASM_MMU,d0				;there is an MMU
	movec.l d1,cacr                 ;restore the cache state
	bra.s	vectorrestoreMMU		;we have a MMU
crashvectorMMU:
	moveq	#0,d0
vectorrestoreMMU:
    move.l	(sp)+,d2				;pop stack (let d0 untouched!
	move.l	a1,IllegalInstructionVector.w	;restore Illegal Instruction and LineF vectors
	move.l	a2,Line1111Vector.w
	rte





    END
