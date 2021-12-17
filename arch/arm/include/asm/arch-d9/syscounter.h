/* SPDX-License-Identifier: GPL-2.0+
 *
 * Semidrive syscounter driver
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#ifndef _ASM_ARCH_SYSTEM_COUNTER_H
#define _ASM_ARCH_SYSTEM_COUNTER_H

#include <linux/bitops.h>

#define SC_CNTCR_ENABLE		BIT(0)
#define ARCH_TIMER_IRQ		30

/* System Counter */
struct sctr_regs {
	u32 cntcr;
	u32 resv1[1];
	u32 cntcv1;
	u32 cntcv2;
	u32 resv2[4];
	u32 cntfid;
};

enum timer_restart {
	TIMER_NORESTART,
	TIMER_RESTART,
};

struct timer {
	int time_ms;
	int (*function)(struct timer *timer);
	int flag;
};

int init_timer(struct timer *timer);
void start_timer(struct timer *timer);
void stop_timer(void);
int mod_timer(struct timer *timer, int time_ms);
void del_timer(void);


#endif
