/* SPDX-License-Identifier: GPL-2.0+
 *
 * (C) Copyright 2003
 */

#include <spi_flash.h>
#include <sdrv/sdrv_partitions.h>

struct partitions *find_sf_partition_by_name(char const *name);

void sf_part_print(struct spi_flash *flash);

int sf_device_init(struct spi_flash *flash);
