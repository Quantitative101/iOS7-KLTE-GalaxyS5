	.syntax unified
	.arm
	.section .text.start, "ax"
	.globl _start
/*
 * Galaxy S5 (MSM8974) XNU stub entry. LK hands over like a Linux kernel (probe, 2026-11-06): SVC mode with IRQ/FIQ masked,
 * r0 = 0, r1 = 0, r2 = DTB at 0x01e00000, running at the header address 0x00008000 (no relocation needed, unlike the
 * Tab 2). Clean+invalidate L1 D by set/way in case LK left the D-cache on (Krait 400 L1 D: 16 KiB, 4 ways, 64 sets of
 * 64-byte lines; the loop below covers that geometry), then MMU and caches off: XNU's locore wants the MMU off.
 */
_start:
	cpsid	if
	mov	r5, #0
1:	mov	r6, #0
2:	orr	r7, r6, r5, lsl #30
	mcr	p15, 0, r7, c7, c14, 2
	add	r6, r6, #64
	cmp	r6, #(64 * 64)
	bne	2b
	add	r5, r5, #1
	cmp	r5, #4
	bne	1b
	dsb
	mrc	p15, 0, r5, c1, c0, 0
	bic	r5, r5, #(1 << 0)		@ M
	bic	r5, r5, #(1 << 2)		@ C
	bic	r5, r5, #(1 << 12)		@ I
	bic	r5, r5, #(1 << 13)		@ V: vectors at VBAR
	mcr	p15, 0, r5, c1, c0, 0
	isb
	mov	r5, #0
	mcr	p15, 0, r5, c7, c5, 0		@ I-cache invalidate
	mcr	p15, 0, r5, c8, c7, 0		@ TLB invalidate
	dsb
	isb
	adr	r5, stub_vectors
	mcr	p15, 0, r5, c12, c0, 0		@ VBAR
	isb
	mrc	p15, 0, r5, c1, c0, 0
	orr	r5, r5, #(1 << 12)		@ I-cache back on (fine with the MMU off)
	mcr	p15, 0, r5, c1, c0, 0
	isb
	/* CPU0 only */
	mrc	p15, 0, r4, c0, c0, 5
	ands	r4, r4, #3
	bne	park
	/* enable CP10/CP11 full access (XNU expects an enabled FPU) */
	mrc	p15, 0, r4, c1, c0, 2
	orr	r4, r4, #(0xf << 20)
	mcr	p15, 0, r4, c1, c0, 2
	isb
	ldr	sp, =0x01000000			@ below LK's DTB (0x01e00000) and ramdisk (0x02000000)
	mov	r0, r2				@ DTB pointer, for crumbs only
	bl	stub_main
park:	wfi
	b	park

/*
 * Exception vectors for the stub itself: each records its number in crumb 15 (0x1f000f3c), the exception lr in 14,
 * DFSR in 13 and DFAR in 12, then waits for a button reset (the stub has set the recovery restart reason, so the phone
 * comes back in TWRP).
 */
	.balign 32
	.globl stub_vectors
stub_vectors:
	b	.			@ reset
	b	v1			@ undefined instruction
	b	v2			@ SVC
	b	v3			@ prefetch abort
	b	v4			@ data abort
	b	v5			@ unused
	b	v6			@ IRQ
	b	v7			@ FIQ
v1:	mov	r12, #1
	b	vfault
v2:	mov	r12, #2
	b	vfault
v3:	mov	r12, #3
	b	vfault
v4:	mov	r12, #4
	b	vfault
v5:	mov	r12, #5
	b	vfault
v6:	mov	r12, #6
	b	vfault
v7:	mov	r12, #7
vfault:
	ldr	r11, =0x1f000f00
	str	r12, [r11, #60]
	str	lr, [r11, #56]
	mrc	p15, 0, r10, c5, c0, 0
	str	r10, [r11, #52]
	mrc	p15, 0, r10, c6, c0, 0
	str	r10, [r11, #48]
	dsb
1:	wfi
	b	1b

/* jump_to_xnu(entry_pa, boot_args_pa): XNU locore wants r0 = physical boot_args, MMU off, SVC mode */
	.globl jump_to_xnu
jump_to_xnu:
	mov	r3, r0
	mov	r0, r1
	mov	r1, #0
	mov	r2, #0
	mov	ip, #0
	mov	lr, #0
	bx	r3

	.section .payload, "a"
	.balign 4096
	.globl xnu_blob_start, xnu_blob_end, dtb_start, dtb_end
xnu_blob_start:
	.incbin "xnu.img"
xnu_blob_end:
	.balign 4
dtb_start:
	.incbin "dtb"
dtb_end:
