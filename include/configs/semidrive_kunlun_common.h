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
