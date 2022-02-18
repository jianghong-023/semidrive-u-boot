/* SPDX-License-Identifier: GPL-2.0+
 *
 * (C) Copyright 2022
 */

#include <errno.h>
#include <part.h>
#include <malloc.h>

#define GPT_PRIORITY 1
#define MAX_PART_COUNT 128

struct partitions {
	char name[PART_NAME_LEN];	/* identifier string */
	u8 num; /* partition num */
	u64 offset;	/* offset within the master space */
	u64 size;	/* partition size */
	unsigned int mask_flags;	/* master flags to mask out for this partition */
};

/* partition table for innor usage*/
struct _iptbl {
	struct partitions *partitions;
	int count;  /* partition count in use */
};

/* iptbl buffer opt. */
static inline int _zalloc_iptbl(struct _iptbl **_iptbl)
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
	memset(partition, 0, sizeof(struct partitions) * MAX_PART_COUNT);
	memset(iptbl, 0, sizeof(struct _iptbl));

	iptbl->partitions = partition;
	pr_info("iptbl %p, partition %p, iptbl->partitions %p\n",
		iptbl, partition, iptbl->partitions);
	*_iptbl = iptbl;
_out:
	return ret;
}

static inline struct partitions *_find_partition_by_name(struct partitions *tbl,
							 int cnt, const char *name)
{
	int i = 0;
	struct partitions *part = NULL;

	if (!tbl)
		goto _out;

	while (i < cnt) {
		part = &tbl[i];
		if (!strcmp(name, part->name)) {
			pr_debug("find %s @ tbl[%d]\n", name, i);
			break;
		}
		i++;
	};
	if (i == cnt) {
		part = NULL;
		pr_err("do not find match in table %s\n", name);
	}

_out:
	return part;
}

static inline struct partitions
*find_partition_by_name(struct _iptbl *p_iptbl_ept, char const *name)
{
	struct partitions *partition = NULL, *part = NULL;

	pr_debug("p_iptbl_ept %p\n", p_iptbl_ept);
	if (!p_iptbl_ept)
		goto _out;

	partition = p_iptbl_ept->partitions;
	pr_debug("partition %p\n", partition);
	part = _find_partition_by_name(partition,
				       p_iptbl_ept->count, name);
	if (!part)
		pr_err("partition is not found\n");
	pr_debug("partition %p\n", part);

_out:
	return part;
}
