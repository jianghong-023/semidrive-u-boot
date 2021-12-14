// SPDX-License-Identifier: GPL-2.0+
/*
 * Semidrive syscounter driver
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#include <common.h>
#include <init.h>
#include <time.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <div64.h>
#include <linux/delay.h>
#include <asm/arch/syscounter.h>

DECLARE_GLOBAL_DATA_PTR;

static inline unsigned long long tick_to_time(unsigned long long tick)
{
	unsigned long freq;

	freq = get_tbclk();
	tick *= CONFIG_SYS_HZ;
	do_div(tick, freq);

	return tick;
}

static inline unsigned long long us_to_tick(unsigned long long usec)
{
	unsigned long freq;

	freq = get_tbclk();
	usec = usec * freq  + 999999;
	do_div(usec, 1000000);

	return usec;
}

ulong get_timer(ulong base)
{
	return tick_to_time(get_ticks()) - base;
}

void __udelay(unsigned long usec)
{
	unsigned long long tmp;
	ulong tmo;

	tmo = us_to_tick(usec);
	tmp = get_ticks() + tmo;	/* get current timestamp */

	while (get_ticks() < tmp)	/* loop till event */
		;
}
