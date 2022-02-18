/* SPDX-License-Identifier: GPL-2.0+
 *
 * (C) Copyright 2003
 */

#include <mmc.h>
#include <sdrv/sdrv_partitions.h>

#ifndef CONFIG_ENV_SIZE
#define CONFIG_ENV_SIZE 0x10000
#endif

struct partitions *find_mmc_partition_by_name(char const *name);

int mmc_device_init(struct mmc *mmc);
