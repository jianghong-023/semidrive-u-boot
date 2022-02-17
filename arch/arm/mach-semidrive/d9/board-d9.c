// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#include <common.h>
#include <init.h>
#include <log.h>
#include <net.h>
#include <asm/arch/boot.h>
#include <asm/arch/eth.h>
#include <asm/arch/mem.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/armv8/mmu.h>
#include <linux/sizes.h>
#include <usb.h>
#include <linux/usb/otg.h>
#include <asm/arch/usb.h>
#include <usb/dwc2_udc.h>
#include <phy.h>
#include <clk.h>

#define  MM_RAM_BASE	0x40000000UL
#define  MM_RAM_SIZE	0xC0000000UL    /* 3GB */

DECLARE_GLOBAL_DATA_PTR;

static struct mm_region d9_ref_mem_map[] = {
	{
		/* RAM */
		.virt = MM_RAM_BASE,
		.phys = MM_RAM_BASE,
		.size = MM_RAM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE
	}, {
		/* IO SPACE(OSPI AHB_MEM) */
		.virt = 0x4000000UL,
		.phys = 0x4000000UL,
		.size = 0x8000000UL,	/* 128MB */
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		/* IO SPACE */
		.virt = 0x30000000UL,
		.phys = 0x30000000UL,
		.size = 0x10000000UL,	/* 256MB */
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE |
			 PTE_BLOCK_PXN | PTE_BLOCK_UXN
	}, {
		/* List terminator */
		0,
	}
};

struct mm_region *mem_map = d9_ref_mem_map;

ulong board_get_usable_ram_top(ulong total_size)
{
	unsigned long top = MM_RAM_BASE + MM_RAM_SIZE;

	return (gd->ram_top > top) ? top : gd->ram_top;
}
