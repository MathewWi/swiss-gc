# Patches into VI Configure to change the video mode to 576p just prior to video being configured
#include "asm.h"
#define _LANGUAGE_ASSEMBLY
#include "../../../reservedarea.h"


.globl ForceProgressive576p
ForceProgressive576p:
	li			%r0, 0
	lis			%r12, 0x8000
	stw			%r0, 0x00CC (%r12)
	li			%r0, 2
	stw			%r0, 0 (%r3)
	lhz			%r0, 16 (%r3)
	subfic		%r0, %r0, 576
	srwi		%r0, %r0, 1
	sth			%r0, 12 (%r3)
	li			%r0, 10
	lis			%r12, VAR_AREA
	mtctr		%r0
	addi		%r12, %r12, VAR_PROG_MODE-4
	addi		%r11, %r3, 20-4
1:	lwzu		%r0, 4 (%r12)
	stwu		%r0, 4 (%r11)
	bdnz		1b
	mflr		%r0
	trap

.globl ForceProgressive576p_length
ForceProgressive576p_length:
.long (ForceProgressive576p_length - ForceProgressive576p)