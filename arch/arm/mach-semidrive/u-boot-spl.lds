/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2021
 * Huanghuafeng@semidrive.com
 */

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
	. = 0x00000000;

	. = ALIGN(8);
	.text : {
		*(.__image_copy_start)
		CPUDIR/start.o (.text*)
		*(.text*)
	}

	. = ALIGN(8);
	.rodata : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	}

	. = ALIGN(8);
	.data : {
		*(.data*)
	}

	. = ALIGN(8);
	.u_boot_list : {
		KEEP(*(SORT(.u_boot_list*)));
	}

	. = ALIGN(8);
	.image_copy_end : {
		*(.__image_copy_end)
	}

	. = ALIGN(8);
	.end : {
		*(.__end)
	}

	_image_binary_end = .;

	. = ALIGN(8);
	__bss_start = .;
	.bss : {
		*(.*bss)
	}
	__bss_end = .;

	/DISCARD/ : { *(.dynsym) }
	/DISCARD/ : { *(.dynstr*) }
	/DISCARD/ : { *(.dynamic*) }
	/DISCARD/ : { *(.plt*) }
	/DISCARD/ : { *(.interp*) }
	/DISCARD/ : { *(.gnu*) }
}
