// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#include <common.h>
#include <command.h>
#include <asm/arch/syscounter.h>

int timer_handler(struct timer *timer)
{
	printf("handler timer interrupt\n");

	/* restart timer depend on the return value of timer handler */
	return timer->flag;
}

/* struct timer variable must global or malloc
 * as irq handler will use this struct
 */

struct timer timer;

static int do_tick(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	if (argc != 2)
		return CMD_RET_USAGE;

	if (!strcmp(argv[1], "period"))
		timer.flag = 1;
	else
		timer.flag = 0;

	timer.time_ms = 1000;
	timer.function = timer_handler;

	/* only init once */
	init_timer(&timer);

	start_timer(&timer);

	/* if want to stop tick */
	//stop_timer();

	/* release timer resource, match with init_timer() */
	//del_timer();

	return 0;
}

static int do_stop_tick(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	stop_timer();
	del_timer();
	return 0;
}

U_BOOT_CMD(
	tick,    2,    1,     do_tick,
	"tick once/period\n",
	"period - handle timer interrupt in 1s."
);

U_BOOT_CMD(
	stop_tick, 1, 0, do_stop_tick,
	"stop_tick\n",
	"test tick stop function"
);
