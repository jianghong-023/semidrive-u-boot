/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for Semidrive Kunlun 64bits SoCs
 */

#ifndef __SDRV_KUNLUN_COMMON_H__
#define __SDRV_KUNLUN_COMMON_H__

/* For splashscreen */
#ifdef CONFIG_DM_VIDEO
#define STDOUT_CFG "vidconsole,serial"
#else
#define STDOUT_CFG "serial"
#endif

#ifdef CONFIG_USB_KEYBOARD
#define STDIN_CFG "usbkbd,serial"
#else
#define STDIN_CFG "serial"
//#define DEBUG 1
#endif

#ifdef CONFIG_SPL_BUILD
/* Define for spl */
#define SPL_MEM_SIZE			0x400000	/* 4M */
#define CONFIG_SPL_MAX_SIZE		0x40000		/* 256k */
#define CONFIG_SPL_BSS_MAX_SIZE		0x20000		/* 128K */
#define SPL_RESERVE_SIZE		0x9000		/* 36K */
#define CONFIG_SPL_BSS_START_ADDR	(CONFIG_SPL_TEXT_BASE + SPL_MEM_SIZE -\
					CONFIG_SPL_BSS_MAX_SIZE)
#define CONFIG_SPL_STACK_R_ADDR		(CONFIG_SPL_BSS_START_ADDR - \
					SPL_RESERVE_SIZE)
#define CONFIG_MALLOC_F_ADDR	(CONFIG_SPL_TEXT_BASE + CONFIG_SPL_MAX_SIZE)
#define CONFIG_DEBUG_UART_BASE	0x30520000
#define CONFIG_DEBUG_UART_CLOCK 60000000
#ifndef CONFIG_SPL_DM
/* As spl without dtb, so it can't use DM mode */
#define CONFIG_CONS_INDEX		1
#define CONFIG_SYS_NS16550_COM1		CONFIG_DEBUG_UART_BASE
#endif
#endif

/* syscounter freq */
#define COUNTER_FREQUENCY	3000000

#define CONFIG_CPU_ARMV8
#define CONFIG_REMAKE_ELF
#define CONFIG_SYS_MAXARGS		32
#define CONFIG_SYS_MALLOC_LEN		(32 << 20)
#define CONFIG_SYS_CBSIZE		1024

#define CONFIG_ARMV8_SWITCH_TO_EL1

#define CONFIG_SYS_INIT_SP_ADDR		0x80000000
#define CONFIG_SYS_LOAD_ADDR		CONFIG_SYS_TEXT_BASE
#define CONFIG_SYS_BOOTM_LEN		(64 << 20) /* 64 MiB */

#ifdef CONFIG_CMD_USB
#define BOOT_TARGET_DEVICES_USB(func) func(USB, usb, 0)
#else
#define BOOT_TARGET_DEVICES_USB(func)
#endif

#ifndef BOOT_TARGET_DEVICES
#define BOOT_TARGET_DEVICES(func) \
	(\
	func(MMC, mmc, 0) \
	func(MMC, mmc, 1) \
	func(MMC, mmc, 2) \
	BOOT_TARGET_DEVICES_USB(func) \
	)
#endif

#endif /* __SDRV_KUNLUN_COMMON_H__ */
