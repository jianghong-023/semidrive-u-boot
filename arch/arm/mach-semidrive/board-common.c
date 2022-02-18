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
#ifdef CONFIG_SPL_MMC_SUPPORT
#include <mmc.h>
#include <sdrv/emmc_partitions.h>
#endif
#ifdef CONFIG_TARGET_D9LITE_REF
#include <dt-bindings/memmap/d9lite/projects/default/image_cfg.h>
#elif CONFIG_TARGET_D9PLUS_AP1_REF
#include <dt-bindings/memmap/d9plus/projects/default/image_cfg.h>
#elif CONFIG_TARGET_D9PLUS_AP2_REF
#include <dt-bindings/memmap/d9plus/projects/default/image_cfg.h>
#else
#include <dt-bindings/memmap/d9/projects/default/image_cfg.h>
#endif

#ifdef CONFIG_TARGET_D9LITE_REF
#define AP_ATF_MEMBASE AP2_ATF_MEMBASE
#elif CONFIG_TARGET_D9PLUS_AP1_REF
#define AP_ATF_MEMBASE AP1_ATF_MEMBASE
#else
#define AP_ATF_MEMBASE AP1_ATF_MEMBASE
#endif

#if defined(CONFIG_TARGET_D9PLUS_AP1_REF) || defined(CONFIG_TARGET_D9PLUS_AP2_REF)
#define IMG_BACKUP_LOW_BASE (DIL_IMAGES_MEMBASE + 0x10000000)
#define IMG_BACKUP_LOW_SIZE DIL_IMAGES_MEMSIZE
#define IMG_BACKUP_HIGH_BASE AP2_IMAGES_MEMBASE
#define IMG_BACKUP_HIGH_SIZE AP2_IMAGES_MEMSIZE
#define AP2_KERNEL_UIMAGE_MEMBASE (AP2_KERNEL_MEMBASE + 0x2000000)

#define IMG_BACKUP_MEMSEEK_SZ 0x440
#define IMG_BACKUP_PRELOADER_SZ 0x100000
#define IMG_BACKUP_BOOTLOADER_SZ 0x200000
#define IMG_BACKUP_ATF_SZ 0x20000
#define IMG_BACKUP_DTB_SZ 0x80000
#define IMG_BACKUP_KERNEL_SZ 0x1800000
#define IMG_BACKUP_RAMDISK_SZ 0x4000000
#define IMG_BACKUP_PRELOADER_OFF (IMG_BACKUP_LOW_BASE + IMG_BACKUP_MEMSEEK_SZ)
#define IMG_BACKUP_ATF_OFF (IMG_BACKUP_HIGH_BASE + IMG_BACKUP_MEMSEEK_SZ)
#define IMG_BACKUP_BOOTLOADER_OFF (IMG_BACKUP_ATF_OFF + IMG_BACKUP_ATF_SZ)
#define IMG_BACKUP_DTB_OFF (IMG_BACKUP_BOOTLOADER_OFF + IMG_BACKUP_BOOTLOADER_SZ)
#define IMG_BACKUP_KERNEL_OFF (IMG_BACKUP_DTB_OFF + IMG_BACKUP_DTB_SZ)
#define IMG_BACKUP_RAMDISK_OFF (IMG_BACKUP_KERNEL_OFF + IMG_BACKUP_KERNEL_SZ)
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

#ifdef CONFIG_SPL_MMC_SUPPORT
int spl_part_load(struct mmc *mmc, char *part_name, void *addr, u64 size)
{
	struct partitions *part;
	u64 blk, cnt, n;

	if (!mmc || !part_name || !addr)
		return -EINVAL;

	part = find_mmc_partition_by_name(part_name);
	if (!part) {
		pr_err("part get fail\n");
		return -EINVAL;
	}
	blk = part->offset;
	cnt = part->size;
	if (size)
		cnt = (size + mmc->read_bl_len - 1) / mmc->read_bl_len;
	if (cnt > part->size)
		cnt = part->size;

	pr_debug("part: blk = %x, cnt = %x\n", blk, cnt);

	n = blk_dread(mmc_get_blk_desc(mmc), blk, cnt, addr);
	if (n != cnt) {
		pr_err("mmc read %s fail!\n", part_name);
		return -EINVAL;
	}

	return 0;
}

int spl_load_ap(struct mmc *mmc)
{
	struct partitions *part;
	void *addr;
	int ret = 0;

	if (!mmc)
		return -ENODEV;

	addr = (void *)AP_ATF_MEMBASE;
	ret = spl_part_load(mmc, "atf_a", addr, 0);
	if (ret) {
		pr_err("atf part read fail\n");
		return -EINVAL;
	}

	addr = (void *)CONFIG_SYS_TEXT_BASE;
	ret = spl_part_load(mmc, "bootloader_a", addr, 0);
	if (ret) {
		pr_err("bootloader part read fail\n");
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_TARGET_D9PLUS_AP1_REF
int spl_load_ap2(struct mmc *mmc)
{
	struct partitions *part;
	void *addr;
	int ret = 0, fmt = 0;

	if (!mmc)
		return -ENODEV;

	addr = (void *)IMG_BACKUP_PRELOADER_OFF;
	ret = spl_part_load(mmc, "cluster_preloader_a", addr, IMG_BACKUP_PRELOADER_SZ);
	if (ret) {
		pr_err("ap2_preloader part read fail\n");
		return -EINVAL;
	}

	addr = (void *)IMG_BACKUP_ATF_OFF;
	ret = spl_part_load(mmc, "cluster_atf_a", addr, IMG_BACKUP_ATF_SZ);
	if (ret) {
		pr_err("ap2_atf part read fail\n");
		return -EINVAL;
	}

	addr = (void *)IMG_BACKUP_BOOTLOADER_OFF;
	ret = spl_part_load(mmc, "cluster_bootloader_a", addr, IMG_BACKUP_PRELOADER_SZ);
	if (ret) {
		pr_err("ap2_bootloader part read fail\n");
		return -EINVAL;
	}

	addr = (void *)IMG_BACKUP_KERNEL_OFF;
	ret = spl_part_load(mmc, "cluster_kernel_a", addr, IMG_BACKUP_KERNEL_SZ);
	if (ret) {
		pr_err("ap2_kernel part read fail\n");
		return -EINVAL;
	}
	fmt = genimg_get_format((void *)IMG_BACKUP_KERNEL_OFF);
	printf("fmt = %d\n", fmt);

	if (fmt != IMAGE_FORMAT_FIT) {
		addr = (void *)IMG_BACKUP_DTB_OFF;
		ret = spl_part_load(mmc, "cluster_dtb_a", addr, IMG_BACKUP_DTB_SZ);
		if (ret) {
			pr_err("ap2_dtb part read fail\n");
			return -EINVAL;
		}

		addr = (void *)IMG_BACKUP_RAMDISK_OFF;
		ret = spl_part_load(mmc, "cluster_ramdisk_a", addr, IMG_BACKUP_RAMDISK_SZ);
		if (ret) {
			pr_err("ap2_ramdisk part read fail\n");
			return -EINVAL;
		}
	}

	addr = (void *)IMG_BACKUP_PRELOADER_OFF;
	memcpy((void *)AP2_PRELOADER_MEMBASE, addr, IMG_BACKUP_PRELOADER_SZ);

	return 0;
}
#endif

#ifdef CONFIG_SPL_MMC_SUPPORT
int spl_emmc_load_image(int dev_num)
{
	struct mmc *mmc;
	struct blk_desc *mmc_dev;
	int ret = 0;

	mmc_initialize(NULL);

	mmc = find_mmc_device(dev_num);
	if (!mmc) {
		pr_err("no mmc device at slot\n");
		return -ENODEV;
	}

	mmc_dev = blk_get_devnum_by_type(IF_TYPE_MMC, dev_num);
	if (!mmc_dev || mmc_dev->type == DEV_TYPE_UNKNOWN) {
		pr_err("mmc blk_dev get fail\n");
		return -ENODEV;
	}

	ret = spl_load_ap(mmc);
	if (ret) {
		pr_err("spl load AP1 fail\n");
		return -EINVAL;
	}
#ifdef CONFIG_TARGET_D9PLUS_AP1_REF
	ret = spl_load_ap2(mmc);
	if (ret) {
		pr_err("spl load AP2 backup fail\n");
		return -EINVAL;
	}
#endif

	return 0;
}
#endif
#endif

#ifdef CONFIG_TARGET_D9PLUS_AP2_REF
int spl_ap2_run(void)
{
	void *addr;
	int ret = 0, fmt = 0;

	addr = (void *)IMG_BACKUP_ATF_OFF;
	memcpy((void *)AP2_ATF_MEMBASE, addr, IMG_BACKUP_ATF_SZ);

	addr = (void *)IMG_BACKUP_BOOTLOADER_OFF;
	memcpy((void *)AP2_BOOTLOADER_MEMBASE, addr, IMG_BACKUP_BOOTLOADER_SZ);

	fmt = genimg_get_format((void *)IMG_BACKUP_KERNEL_OFF);
	printf("fmt = %d\n", fmt);

	if (fmt == IMAGE_FORMAT_FIT) {
		addr = (void *)IMG_BACKUP_KERNEL_OFF;
		memcpy((void *)AP2_KERNEL_UIMAGE_MEMBASE, addr, IMG_BACKUP_KERNEL_SZ);
	} else {
		addr = (void *)IMG_BACKUP_DTB_OFF;
		memcpy((void *)AP2_REE_MEMBASE, addr, IMG_BACKUP_DTB_SZ);

		addr = (void *)IMG_BACKUP_KERNEL_OFF;
		memcpy((void *)AP2_KERNEL_MEMBASE, addr, IMG_BACKUP_KERNEL_SZ);

		addr = (void *)IMG_BACKUP_RAMDISK_OFF;
		memcpy((void *)AP2_BOARD_RAMDISK_MEMBASE, addr, IMG_BACKUP_RAMDISK_SZ);
	}

	return 0;
}
#endif

typedef void (*bl31_entry_t)(uintptr_t bl32_entry,
		uintptr_t bl33_entry, uintptr_t fdt_addr);

void board_init_r(gd_t *id, ulong dest_addr)
{
	bl31_entry_t atf_entry;

	/* set this addr for reserve 32k buffer for pagetab */
	gd->relocaddr = CONFIG_SPL_BSS_START_ADDR;

	arch_reserve_mmu();

	/* Enable caches */
	enable_caches();

#ifndef CONFIG_TARGET_D9PLUS_AP2_REF
	atf_entry = (bl31_entry_t)AP_ATF_MEMBASE;
#else
	/* atf_entry = (bl31_entry_t)AP2_BOOTLOADER_MEMBASE; */
	atf_entry = (bl31_entry_t)AP2_ATF_MEMBASE;
#endif

#ifndef CONFIG_TARGET_D9PLUS_AP2_REF
	spl_emmc_load_image(0);
#else
	spl_ap2_run();
	/* atf_entry(0, 0, 0); */
#endif

	/* Jump atf */
	pr_debug("Jump atf: %x %x 0\n", atf_entry, CONFIG_SYS_TEXT_BASE);
	/* Be careful, Now we are in el3 mode
	 * so can jump secure monitor derectly, when change the exception mode
	 * of spl,we only can use smc instruction to switch to el3.
	 */
	cleanup_before_boot_other();

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
