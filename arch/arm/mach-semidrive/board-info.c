// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#include <common.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <dm.h>
#include <linux/bitfield.h>
#include <regmap.h>
#include <syscon.h>
#include <linux/bitops.h>
#include <linux/err.h>

static void print_board_model(void)
{
	const char *model;
	model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
	printf("Model: %s\n", model ? model : "Unknown");
}

int show_board_info(void)
{
	/* print board information */
	print_board_model();
	return 0;
}

