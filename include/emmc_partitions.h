/* SPDX-License-Identifier: GPL-2.0+
 *
 * (C) Copyright 2003
 */

#include <part.h>
#include <mmc.h>

#ifndef CONFIG_ENV_SIZE
#define CONFIG_ENV_SIZE 0x10000
#endif

struct partitions {
	char name[PART_NAME_LEN];	/* identifier string */
	u8 num; /* partition num */
	u64 size;	/* partition size */
	u64 offset;	/* offset within the master space */
	unsigned int mask_flags;	/* master flags to mask out for this partition */
};

/* partition table for innor usage*/
struct _iptbl {
	struct partitions *partitions;
	int count;  /* partition count in use */
};

struct partitions *find_mmc_partition_by_name(char const *name);

int mmc_device_init(struct mmc *mmc);
