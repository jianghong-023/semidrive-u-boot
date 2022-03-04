// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2003
 */

#include <blk.h>
#include <errno.h>
#include <part.h>
#include <malloc.h>
#include <emmc_partitions.h>

#define GPT_PRIORITY 1
#define MAX_PART_COUNT 128

/* partition table (Emmc Partition Table) */
struct _iptbl *p_iptbl_ept = NULL;

/* iptbl buffer opt. */
static int _zalloc_iptbl(struct _iptbl **_iptbl)
{
	int ret = 0;
	struct _iptbl *iptbl;
	struct partitions *partition = NULL;

	partition = malloc(sizeof(struct partitions) * MAX_PART_COUNT);
	if (!partition) {
		ret = -1;
		pr_err("no enough memory for partitions\n");
		goto _out;
	}

	iptbl = malloc(sizeof(struct _iptbl));
	if (!iptbl) {
		ret = -2;
		pr_err("no enough memory for ept\n");
		free(partition);
		goto _out;
	}
	memset(partition, 0, sizeof(struct partitions)*MAX_PART_COUNT);
	memset(iptbl, 0, sizeof(struct _iptbl));

	iptbl->partitions = partition;
	pr_info("iptbl %p, partition %p, iptbl->partitions %p\n",
			iptbl, partition, iptbl->partitions);
	*_iptbl = iptbl;
_out:
	return ret;
}

static struct partitions *_find_partition_by_name(struct partitions *tbl,
		int cnt, const char *name)
{
	int i = 0;
	struct partitions *part = NULL;

	while (i < cnt) {
		part = &tbl[i];
		if (!strcmp(name, part->name)) {
			pr_info("find %s @ tbl[%d]\n", name, i);
			break;
		}
		i++;
	};
	if (i == cnt) {
		part = NULL;
		pr_err("do not find match in table %s\n", name);
	}
	return part;
}

struct partitions *find_mmc_partition_by_name(char const *name)
{
	struct partitions *partition = NULL;

	pr_debug("p_iptbl_ept %p\n", p_iptbl_ept);
	if (!p_iptbl_ept)
		goto _out;

	partition = p_iptbl_ept->partitions;
	partition = _find_partition_by_name(partition,
			p_iptbl_ept->count, name);
	pr_info("partition %p\n", partition);
	if (!partition)
		pr_info("partition is not found\n");
	pr_info("partition %p\n", partition);
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

	if (!p_iptbl_ept) {
		ret = _zalloc_iptbl(&p_iptbl_ept);
		if (ret)
			return -ENOMEM;
	} else {
		p_iptbl_ept->count = 0;
		memset(p_iptbl_ept->partitions, 0,
				sizeof(struct partitions) * MAX_PART_COUNT);
	}

	part = p_iptbl_ept->partitions;
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
	p_iptbl_ept->count = i - 1;
	pr_debug("emmc partition count = %d\n", p_iptbl_ept->count);

	/* init part again */
	part_init(mmc_get_blk_desc(mmc));

	return 0;
}

