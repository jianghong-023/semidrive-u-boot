/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _D9_PLUS_AP1_REF_CONFIG_H_
#define _D9_PLUS_AP1_REF_CONFIG_H_

#include "semidrive_kunlun_common.h"

#define GICD_BASE			(0x35431000)
#define GICC_BASE			(0x35432000)

#include <config_distro_bootcmd.h>

#ifndef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
	"stdin=" STDIN_CFG "\0" \
	"stdout=" STDOUT_CFG "\0" \
	"stderr=" STDOUT_CFG "\0" \
	"fdt_addr_r=0x57800000\0" \
	"kernel_addr_r=0x59A80000\0" \
	"kernel_comp_addr_r=0x5A000000\0" \
	"kernel_comp_size=0x1000000\0" \
	"fdtfile=semidrive/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	"load_kernel_dtb=mmc dev 0; mmc part_read dtb_a ${fdt_addr_r}\0" \
	"mmc_boot=" \
		"mmc dev 0; mmc part_read kernel_a ${kernel_addr_r};" \
		"fdt addr ${fdt_addr_r};" \
		"if test $? -ne 0; then " \
			"mmc part_read dtb_a ${fdt_addr_r};" \
		"fi;" \
		"bootm ${kernel_addr_r}#conf@2 - ${fdt_addr_r}\0" \
	"bootcmd=run mmc_boot;\0"
#endif

#endif /* _D9_PLUS_AP1_REF_CONFIG_H_ */
