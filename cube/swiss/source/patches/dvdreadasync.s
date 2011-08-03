#include "asm.h"

# issue read command
#
#	r3	dvdstruct
#	r4	dst	
#	r5	len
#	r6	off
#	r7	cb

.globl DVDReadAsync
DVDReadAsync:

	stwu    %sp, -0x10(%sp)
	mflr    %r0
	stw     %r0, 8(%sp)

	stw     %r7,	0x28(%r3)
		
#Call our code to do the load instead

	mfmsr	%r7
	rlwinm	%r7,%r7,0,17,15
	mtmsr	%r7
	
	lis		%r7,	0x8000
	ori		%r7,	%r7,	0x180C
	mtctr	%r7
	bctrl
	
	mfmsr	%r7
	ori		%r7,%r7,0x8000
	mtmsr	%r7
			
	lwz     %r7,	0x28(%r3)
		
#update dvdstruct

	li		%r0,	0
	stw     %r0,	0x00(%r3)
	stw     %r0,	0x04(%r3)
	stw     %r0,	0x0C(%r3)
	stw     %r0,	0x20(%r3)

	li		%r0,	1
	stw     %r0,	0x08(%r3)

#offset
	lwz     %r0,	0x30(%r3)
	stw     %r0,	0x10(%r3)

#size
	#lwz     %r0,	0x2F40(%r7)
	stw     %r5,	0x14(%r3)

#ptr
	stw     %r4,	0x18(%r3)
	
#TransferSize
	stw     %r5,	0x1C(%r3)
	
#callback
	cmpwi	%r7,	0
	beq		skip_cb
	mtctr	%r7
	mr		%r4,	%r3
	mr		%r3,	%r5
	bctrl

skip_cb:
	
	li      %r3,	1

	lwz     %r0, 8(%sp)
	mtlr    %r0
	addi    %sp, %sp, 0x10
	blr
   .globl DVDReadAsync_length
   DVDReadAsync_length:
   .long (DVDReadAsync_length - DVDReadAsync)