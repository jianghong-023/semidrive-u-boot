// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2022
 */

#include <blk.h>
#include <errno.h>
#include <part.h>
#include <malloc.h>
#include <sdrv/emmc_partitions.h>

/* partition table (Emmc Partition Table) */
struct _iptbl *p_mmc_ept;

struct partitions *find_mmc_partition_by_name(char const *name)
{
	struct partitions *partition = NULL;

	pr_debug("p_mmc_ept %p\n", p_mmc_ept);
	if (!p_mmc_ept)
		goto _out;

	partition = find_partition_by_name(p_mmc_ept, name);

_out:
	return partition;
}

int mmc_get_env_addr(struct mmc *mmc, int copy, u32 *env_addr)
{
	struct partitions *partition = NULL;

	if (!mmc)
		return -EINVAL;

	partition = find_mmc_partition_by_name("env_a");
	if (!partition) {
		pr_err("env partition is not found\n");
		return -EINVAL;
	}

	*env_addr = partition->offset * mmc->read_bl_len;
	if (copy) {
		partition = find_mmc_partition_by_name("env_b");
		if (!partition) {
			pr_err("env partition is not found\n");
			return -EINVAL;
		}

		*env_addr = partition->offset * mmc->read_bl_len;
	}

	return 0;
}

int mmc_device_init(struct mmc *mmc)
{
	int ret = 0, i = 0;
	struct disk_partition disk_partition;
	struct partitions *part;
	struct blk_desc *dev_desc = NULL;

	if (!p_mmc_ept) {
		ret = _zalloc_iptbl(&p_mmc_ept);
		if (ret) {
			pr_err("mmc partition cannot allocate memory\n");
			return -ENOMEM;
		}
	} else {
		p_mmc_ept->count = 0;
		memset(p_mmc_ept->partitions, 0,
		       sizeof(struct partitions) * MAX_PART_COUNT);
	}

	part = p_mmc_ept->partitions;
	dev_desc = mmc_get_blk_desc(mmc);
	for (i = 1; i < MAX_PART_COUNT; i++) {
		memset(&disk_partition, 0, sizeof(struct disk_partition));
		ret = part_get_info(dev_desc, i, &disk_partition);
		if (ret)
			break;
		pr_debug("part_name = %s, offset = %lx, size = %lx\n",
				disk_partition.name,
				disk_partition.start,
				disk_partition.size);

		strncpy(part->name, (char *)disk_partition.name,
				strlen((char *)disk_partition.name));
		part->num = i;
		part->offset = disk_partition.start;
		part->size = disk_partition.size;
		part++;
	}
	p_mmc_ept->count = i - 1;
	pr_debug("emmc partition count = %d\n", p_mmc_ept->count);

	/* init part again */
	part_init(mmc_get_blk_desc(mmc));

	return 0;
}

