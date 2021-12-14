/* SPDX-License-Identifier: GPL-2.0+
 *
 * Semidrive syscounter driver
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#ifndef _ASM_ARCH_SYSTEM_COUNTER_H
#define _ASM_ARCH_SYSTEM_COUNTER_H

#include <linux/bitops.h>

/* System Counter */
struct sctr_regs {
	u32 cntcr;
	u32 resv1[1];
	u32 cntcv1;
	u32 cntcv2;
	u32 resv2[4];
	u32 cntfid;
};

#define SC_CNTCR_ENABLE		BIT(0)

#endif
