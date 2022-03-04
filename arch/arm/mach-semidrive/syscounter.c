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
#include <interrupts.h>

DECLARE_GLOBAL_DATA_PTR;

static inline unsigned long long tick_to_time(unsigned long long tick)
{
	unsigned long freq;

	freq = get_tbclk();
	tick *= CONFIG_SYS_HZ;
	do_div(tick, freq);

	return tick;
}

ulong get_timer(ulong base)
{
	return tick_to_time(get_ticks()) - base;
}

void __udelay(unsigned long usec)
{
	unsigned long long tmp;
	ulong tmo;

	tmo = usec2ticks(usec);
	tmp = get_ticks() + tmo;	/* get current timestamp */

	while (get_ticks() < tmp)	/* loop till event */
		;
}

#ifdef CONFIG_SYSCOUNTER_IRQ
void timer_irq_handler(void *arg)
{
	struct timer *timer = (struct timer *)arg;
	int flag;

	flag = timer->function(timer);
	if (timer->flag == TIMER_RESTART)
		start_timer(timer);
	else
		stop_timer();
}

int init_timer(struct timer *timer)
{
	int ret;

	ret = request_irq(ARCH_TIMER_IRQ, timer_irq_handler,
			  (void *)timer, IRQ_TYPE_LEVEL_HIGH);
	if (ret < 0) {
		printf("request irq error!\n");
		return ret;
	}

	return 0;
}

void start_timer(struct timer *timer)
{
	unsigned long value, cmp;

	enable_irq(ARCH_TIMER_IRQ);
	value = 0;
	asm volatile("msr cntp_ctl_el0, %0" : : "r" (value));

	cmp = get_ticks() + (get_tbclk() / 1000) * timer->time_ms;
	asm volatile("msr cntp_cval_el0, %0" : : "r" (cmp));

	value = 1;
	asm volatile("msr cntp_ctl_el0, %0" : : "r" (value));
}

void stop_timer(void)
{
	unsigned long value = 0;

	asm volatile("msr cntp_ctl_el0, %0" : : "r" (value));
	disable_irq(ARCH_TIMER_IRQ);
}

int mod_timer(struct timer *timer, int time_ms)
{
	timer->time_ms = time_ms;
	return 0;
}

void del_timer(void)
{
	free_irq(ARCH_TIMER_IRQ);
}
#endif
