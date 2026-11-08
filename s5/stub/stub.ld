ENTRY(_start)
SECTIONS
{
	. = 0x00008000;
	.text : { *(.text.start) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(4096);
	.payload : { *(.payload) }
	_image_end = .;
}
