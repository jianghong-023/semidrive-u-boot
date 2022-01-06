// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#include <common.h>
#include <cpu_func.h>
#include <fastboot.h>
#include <init.h>
#include <net.h>
#include <asm/arch/boot.h>
#include <env.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/ptrace.h>
#include <linux/libfdt.h>
#include <linux/err.h>
#include <asm/arch/mem.h>
#include <asm/armv8/mmu.h>
#include <asm/unaligned.h>
#include <efi_loader.h>
#include <u-boot/crc.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <wdt.h>

#if CONFIG_IS_ENABLED(FASTBOOT)
#include <asm/psci.h>
#include <fastboot.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

__weak int board_init(void)
{
	run_command("gpio status", 0);
	return 0;
}

int dram_init(void)
{
	if (fdtdec_setup_mem_size_base() != 0)
		return -EINVAL;

	return 0;
}

int dram_init_banksize(void)
{
	fdtdec_setup_memory_banksize();

	return 0;
}

__weak int ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}

int board_late_init(void)
{
	return 0;
}

void semidrive_wdt_reset(void)
{
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_WDT, &uc);
	if (ret) {
		printf("get wdt uclass failed\n");
		return;
	}

	uclass_foreach_dev(dev, uc) {
		if (dev->driver->priv_auto)
			wdt_expire_now(dev, 0);
	}
}

#if CONFIG_IS_ENABLED(FASTBOOT)
static unsigned int reboot_reason = REBOOT_REASON_NORMAL;

int fastboot_set_reboot_flag(enum fastboot_reboot_reason reason)
{
	if (reason != FASTBOOT_REBOOT_REASON_BOOTLOADER)
		return -ENOTSUPP;

	reboot_reason = REBOOT_REASON_BOOTLOADER;

	printf("Using reboot reason: 0x%x\n", reboot_reason);

	return 0;
}

void reset_cpu(ulong addr)
{
	struct pt_regs regs;

	regs.regs[0] = ARM_PSCI_0_2_FN_SYSTEM_RESET;
	regs.regs[1] = reboot_reason;

	printf("Rebooting with reason: 0x%lx\n", regs.regs[1]);

	semidrive_wdt_reset();
	smc_call(&regs);

	while (1)
		;
}
#else
void reset_cpu(ulong addr)
{
	semidrive_wdt_reset();
	psci_system_reset();
}
#endif
