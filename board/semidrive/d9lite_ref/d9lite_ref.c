// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <env_internal.h>
#include <init.h>
#include <net.h>
#include <asm/io.h>

int misc_init_r(void)
{
	if (!env_get("serial#"))
		env_set("serial#", "SDRV-D9LITE-D9310");

	return 0;
}
