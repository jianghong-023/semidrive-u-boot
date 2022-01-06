// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#include <common.h>
#include <cpu_func.h>
#include <fastboot.h>
#include <init.h>
#include <net.h>
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

#ifdef CONFIG_SPL_BUILD
#include <debug_uart.h>
#include <spl.h>
#else
#include <asm/arch/boot.h>
#endif

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

#ifndef CONFIG_SPL_BUILD
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
#endif

#ifdef CONFIG_SPL_BUILD
u32 spl_boot_device(void)
{
	return 1;
}

void spl_display_print(void)
{
}

#ifndef CONFIG_SPL_FRAMEWORK_BOARD_INIT_F

void board_init_f(ulong dummy)
{
#ifdef CONFIG_DEBUG_UART
	/*
	 * Debug UART can be used from here if required:
	 *
	 * debug_uart_init();
	 * printch('a');
	 * printhex8(0x1234);
	 * printascii("string");
	 */
	debug_uart_init();
	puts("\nspl:debug uart enabled in\n");
#endif
	preloader_console_init();
}
#endif	/* End of CONFIG_SPL_FRAMEWORK_BOARD_INIT_F */

int cleanup_before_boot_other(void)
{
	/*
	 * this function is called just before we call other
	 *
	 * disable interrupt and turn off caches etc ...
	 */

	disable_interrupts();

	/*
	 * Turn off I-cache and invalidate it
	 */
	icache_disable();
	invalidate_icache_all();

	/*
	 * turn off D-cache
	 * dcache_disable() in turn flushes the d-cache and disables MMU
	 */
	dcache_disable();
	invalidate_dcache_all();

	return 0;
}

typedef void (*bl31_entry_t)(uintptr_t bl32_entry,
		uintptr_t bl33_entry, uintptr_t fdt_addr);

void board_init_r(gd_t *id, ulong dest_addr)
{
	bl31_entry_t atf_entry = (bl31_entry_t)CONFIG_ECO_ATF_MEMBASE;

	cleanup_before_boot_other();

	/* Jump atf */
	puts("Jump atf\n");
	/* Be careful, Now we are in el3 mode
	 * so can jump secure monitor derectly, when change the exception mode
	 * of spl,we only can use smc instruction to switch to el3.
	 */
	atf_entry(0, CONFIG_SYS_TEXT_BASE, 0);
	while (1)
		;
}

#endif	/* CONFIG_SPL_BUILD */

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
#elif defined CONFIG_SPL_BUILD
__weak void reset_cpu(ulong addr) {}
#else
void reset_cpu(ulong addr)
{
	semidrive_wdt_reset();
	psci_system_reset();
}
#endif
