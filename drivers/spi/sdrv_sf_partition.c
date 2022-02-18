// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2022
 */

#include <blk.h>
#include <errno.h>
#include <part.h>
#include <malloc.h>
#include <part_efi.h>
#include <linux/compiler.h>
#include <linux/ctype.h>
#include <u-boot/crc.h>
#include <spi_flash.h>
#include <sdrv/sf_partitions.h>

#define GPT_PRIORITY 1
#define MAX_PART_COUNT 128

#define OSPI_SFS_OFF 0
#define OSPI_SFS_SZ 0x1000
#define OSPI_TRANING_OFF 0x1000
#define OSPI_TRANING_SZ 0x1000
#define OSPI_PTN_OFF 0x2000
#define OSPI_PTN_MBR_SZ 0x200
#define OSPI_PTN_GH_SZ 0x200
#define OSPI_PTN_GE_SZ 0x4000
#define OSPI_PTN_SZ 0x4400
#define OSPI_BLK_SZ 0x200

/* partition table (SF Partition Table) */
struct _iptbl *p_sf_ept;
void *part_buf;
void *p_mbr_buf;
void *p_gh_buf;
void *p_ge_buf;
legacy_mbr *pmbr;
gpt_header *pgpt_head;
gpt_entry *pgpt_pte;

static inline u32 _efi_crc32(const void *buf, u32 len)
{
	return crc32(0, buf, len);
}

static char *_print_efiname(gpt_entry *pte)
{
	int i;
	u8 c;
	static char name[PARTNAME_SZ + 1];

	for (i = 0; i < PARTNAME_SZ; i++) {
		c = pte->partition_name[i] & 0xff;
		c = (c && !isprint(c)) ? '.' : c;
		name[i] = c;
	}
	name[PARTNAME_SZ] = 0;

	return name;
}

static const efi_guid_t _system_guid = PARTITION_SYSTEM_GUID;

static int _get_bootable(gpt_entry *p)
{
	int ret = 0;

	if (!memcmp(&p->partition_type_guid, &_system_guid, sizeof(efi_guid_t)))
		ret |=  PART_EFI_SYSTEM_PARTITION;
	if (p->attributes.fields.legacy_bios_bootable)
		ret |=  PART_BOOTABLE;
	return ret;
}

static int _validate_gpt_header(gpt_header *gpt_h, lbaint_t lba,
				lbaint_t lastlba)
{
	u32 crc32_backup = 0;
	u32 calc_crc32;

	/* Check the GPT header signature */
	if (le64_to_cpu(gpt_h->signature) != GPT_HEADER_SIGNATURE_UBOOT) {
		pr_err("%s signature is wrong: 0x%llX != 0x%llX\n",
		       "GUID Partition Table Header",
		       le64_to_cpu(gpt_h->signature),
		       GPT_HEADER_SIGNATURE_UBOOT);
		return -1;
	}

	/* Check the GUID Partition Table CRC */
	memcpy(&crc32_backup, &gpt_h->header_crc32, sizeof(crc32_backup));
	memset(&gpt_h->header_crc32, 0, sizeof(gpt_h->header_crc32));

	calc_crc32 = _efi_crc32((const unsigned char *)gpt_h,
				le32_to_cpu(gpt_h->header_size));

	memcpy(&gpt_h->header_crc32, &crc32_backup, sizeof(crc32_backup));

	if (calc_crc32 != le32_to_cpu(crc32_backup)) {
		pr_err("%s CRC is wrong: 0x%x != 0x%x\n",
		       "GUID Partition Table Header",
		       le32_to_cpu(crc32_backup), calc_crc32);
		return -1;
	}

	/*
	 * Check that the my_lba entry points to the LBA that contains the GPT
	 */
	if (le64_to_cpu(gpt_h->my_lba) != lba) {
		pr_err("GPT: my_lba incorrect: %llX != " LBAF "\n",
		       le64_to_cpu(gpt_h->my_lba),
		       lba);
		return -1;
	}

	/*
	 * Check that the first_usable_lba and that the last_usable_lba are
	 * within the disk.
	 */
	if (le64_to_cpu(gpt_h->first_usable_lba) > lastlba) {
		pr_err("GPT: first_usable_lba incorrect: %llX > " LBAF "\n",
		       le64_to_cpu(gpt_h->first_usable_lba), lastlba);
		return -1;
	}
	if (le64_to_cpu(gpt_h->last_usable_lba) > lastlba) {
		pr_err("GPT: last_usable_lba incorrect: %llX > " LBAF "\n",
		       le64_to_cpu(gpt_h->last_usable_lba), lastlba);
		return -1;
	}

	pr_debug("GPT: first_usable_lba: %llX last_usable_lba: %llX last lba: "
	      LBAF "\n", le64_to_cpu(gpt_h->first_usable_lba),
	      le64_to_cpu(gpt_h->last_usable_lba), lastlba);

	return 0;
}

static int _validate_gpt_entries(gpt_header *gpt_h, gpt_entry *gpt_e)
{
	u32 calc_crc32;

	/* Check the GUID Partition Table Entry Array CRC */
	calc_crc32 = _efi_crc32((const unsigned char *)gpt_e,
				le32_to_cpu(gpt_h->num_partition_entries) *
				le32_to_cpu(gpt_h->sizeof_partition_entry));

	if (calc_crc32 != le32_to_cpu(gpt_h->partition_entry_array_crc32)) {
		pr_err("%s: 0x%x != 0x%x\n",
		       "GUID Partition Table Entry Array CRC is wrong",
		       le32_to_cpu(gpt_h->partition_entry_array_crc32),
		       calc_crc32);
		return -1;
	}

	return 0;
}

int _is_valid_gpt_buf(struct blk_desc *desc, void *buf)
{
	gpt_header *gpt_h;
	gpt_entry *gpt_e;

	/* determine start of GPT Header in the buffer */
	gpt_h = buf + (GPT_PRIMARY_PARTITION_TABLE_LBA *
		       desc->blksz);
	if (_validate_gpt_header(gpt_h, GPT_PRIMARY_PARTITION_TABLE_LBA,
				 desc->lba)) {
		pr_err("_validate_gpt_header fail\n");
		return -1;
	}

	/* determine start of GPT Entries in the buffer */
	gpt_e = buf + (le64_to_cpu(gpt_h->partition_entry_lba) *
		       desc->blksz);
	if (_validate_gpt_entries(gpt_h, gpt_e)) {
		pr_err("_validate_gpt_entries fail\n");
		return -1;
	}

	return 0;
}

static int _is_gpt_valid(struct blk_desc *desc,
			 struct spi_flash *flash, u64 lba)
{
	int ret = 0;
	u32 off;
	size_t count = 0;

	/* Confirm valid arguments prior to allocation. */
	if (!desc || !flash) {
		pr_err("%s: Invalid Argument(s)\n", __func__);
		return 0;
	}

	/* Read MBR Header from device */
	p_mbr_buf = part_buf;
	off = OSPI_PTN_OFF;
	ret = spi_flash_read(flash, off, OSPI_PTN_MBR_SZ, p_mbr_buf);
	if (ret) {
		pr_err("sf read partition MBR fail\n");
		return 0;
	}
	pmbr = (legacy_mbr *)p_mbr_buf;

	/* Read GPT Header from device */
	p_gh_buf = part_buf + OSPI_PTN_MBR_SZ;
	off = OSPI_PTN_OFF + lba * desc->blksz;
	ret = spi_flash_read(flash, off, OSPI_PTN_GH_SZ, p_gh_buf);
	if (ret) {
		pr_err("sf read partition GPT Header fail\n");
		return 0;
	}
	pgpt_head = (gpt_header *)p_gh_buf;

	/* Invalid but nothing to yell about. */
	if (le64_to_cpu(pgpt_head->signature) == GPT_HEADER_CHROMEOS_IGNORE) {
		pr_err("ChromeOS 'IGNOREME' GPT header found and ignored\n");
		return 2;
	}

	if (_validate_gpt_header(pgpt_head, (lbaint_t)lba, desc->lba)) {
		pr_err("validate gpt header fail\n");
		return 0;
	}

	if (desc->sig_type == SIG_TYPE_NONE) {
		efi_guid_t empty = {};

		if (memcmp(&pgpt_head->disk_guid, &empty, sizeof(empty))) {
			desc->sig_type = SIG_TYPE_GUID;
			memcpy(&desc->guid_sig, &pgpt_head->disk_guid,
			       sizeof(empty));
		} else if (pmbr->unique_mbr_signature != 0) {
			desc->sig_type = SIG_TYPE_MBR;
			desc->mbr_sig = pmbr->unique_mbr_signature;
		}
	}

	count = le32_to_cpu(pgpt_head->num_partition_entries) *
		le32_to_cpu(pgpt_head->sizeof_partition_entry);

	pr_debug("%s: count = %u * %u = %lu\n", __func__,
		 (u32)le32_to_cpu(pgpt_head->num_partition_entries),
		 (u32)le32_to_cpu(pgpt_head->sizeof_partition_entry),
		 (ulong)count);

	if (!count) {
		pr_err("%s: ERROR: Can't allocate %#lX bytes for GPT Entries\n",
		       __func__, (ulong)count);
		return 0;
	}

	/* Read GPT Entries from device */
	p_ge_buf = part_buf + OSPI_PTN_MBR_SZ + OSPI_PTN_GH_SZ;
	off = OSPI_PTN_OFF + le64_to_cpu(pgpt_head->partition_entry_lba) * desc->blksz;
	ret = spi_flash_read(flash, off, OSPI_PTN_GE_SZ, p_ge_buf);
	if (ret) {
		pr_err("sf read partition GPT Entries fail\n");
		return 0;
	}
	pgpt_pte = (gpt_entry *)p_ge_buf;

	if (_validate_gpt_entries(pgpt_head, pgpt_pte)) {
		pr_err("validate gpt entries fail\n");
		return 0;
	}

	/* We're done, all's well */
	return 1;
}

/**
 * _find_valid_gpt() - finds a valid GPT header and PTEs
 *
 * gpt is a GPT header ptr, filled on return.
 * ptes is a PTEs ptr, filled on return.
 *
 * Description: returns 1 if found a valid gpt,  0 on error.
 * If valid, returns pointers to PTEs.
 */
static int _find_valid_gpt(struct blk_desc *desc, struct spi_flash *flash)
{
	int r;

	r = _is_gpt_valid(desc, flash, GPT_PRIMARY_PARTITION_TABLE_LBA);

	if (r != 1) {
		if (r != 2)
			pr_err("%s: *** ERROR: Invalid GPT ***\n", __func__);

		if (_is_gpt_valid(desc, flash, (desc->lba - 1))) {
			pr_err("%s: *** ERROR: Invalid Backup GPT ***\n",
			       __func__);
			return 0;
		}
		if (r != 2)
			pr_err("%s: ***        Using Backup GPT ***\n",
			       __func__);
	}
	return 1;
}

static int _is_pte_valid(gpt_entry *pte)
{
	efi_guid_t unused_guid;

	if (!pte) {
		pr_err("%s: Invalid Argument(s)\n", __func__);
		return 0;
	}

	/* Only one validation for now:
	 * The GUID Partition Type != Unused Entry (ALL-ZERO)
	 */
	memset(unused_guid.b, 0, sizeof(unused_guid.b));

	if (memcmp(pte->partition_type_guid.b, unused_guid.b,
		   sizeof(unused_guid.b)) == 0) {
		pr_debug("%s: Found an unused PTE GUID at 0x%08X\n", __func__,
			 (unsigned int)(uintptr_t)pte);

		return 0;
	} else {
		return 1;
	}
}

int _part_get_info_efi(struct blk_desc *desc, struct spi_flash *flash,
		       int part, struct disk_partition *info)
{
	/* "part" argument must be at least 1 */
	if (part < 1) {
		pr_err("%s: Invalid Argument(s)\n", __func__);
		return -1;
	}

	/* This function validates AND fills in the GPT header and PTE */
	if (_find_valid_gpt(desc, flash) != 1)
		return -1;

	if (part > le32_to_cpu(pgpt_head->num_partition_entries) ||
	    !_is_pte_valid(&pgpt_pte[part - 1])) {
		pr_debug("%s: *** ERROR: Invalid partition number %d ***\n",
			 __func__, part);
		return -1;
	}

	/* The 'lbaint_t' casting may limit the maximum disk size to 2 TB */
	info->start = (lbaint_t)le64_to_cpu(pgpt_pte[part - 1].starting_lba);
	/* The ending LBA is inclusive, to calculate size, add 1 to it */
	info->size = (lbaint_t)le64_to_cpu(pgpt_pte[part - 1].ending_lba) + 1
		     - info->start;
	info->blksz = desc->blksz;

	snprintf((char *)info->name, sizeof(info->name), "%s",
		 _print_efiname(&pgpt_pte[part - 1]));
	strcpy((char *)info->type, "U-Boot");
	info->bootable = _get_bootable(&pgpt_pte[part - 1]);
#if CONFIG_IS_ENABLED(PARTITION_UUIDS)
	uuid_bin_to_str(pgpt_pte[part - 1].unique_partition_guid.b, info->uuid,
			UUID_STR_FORMAT_GUID);
#endif
#ifdef CONFIG_PARTITION_TYPE_GUID
	uuid_bin_to_str(pgpt_pte[part - 1].partition_type_guid.b,
			info->type_guid, UUID_STR_FORMAT_GUID);
#endif

	pr_debug("%s: start 0x" LBAF ", size 0x" LBAF ", name %s\n", __func__,
		 info->start, info->size, info->name);

	return 0;
}

struct partitions *find_sf_partition_by_name(char const *name)
{
	struct partitions *partition = NULL;

	pr_debug("p_sf_ept %p\n", p_sf_ept);
	if (!p_sf_ept)
		goto _out;

	partition = find_partition_by_name(p_sf_ept, name);

_out:
	return partition;
}

void sf_part_print(struct spi_flash *flash)
{
	int i = 0;
	char uuid[UUID_STR_LEN + 1];
	unsigned char *uuid_bin;
	struct blk_desc sf_desc;

	sf_desc.blksz = OSPI_BLK_SZ;
	sf_desc.lba = (flash->size - OSPI_PTN_OFF) / sf_desc.blksz - 1;

	/* This function validates AND fills in the GPT header and PTE */
	if (_find_valid_gpt(&sf_desc, flash) != 1)
		return;

	debug("%s: gpt-entry at %p\n", __func__, pgpt_pte);

	printf("Part\tStart LBA\tEnd LBA\t\tName\n");
	printf("\tAttributes\n");
	printf("\tType GUID\n");
	printf("\tPartition GUID\n");

	for (i = 0; i < le32_to_cpu(pgpt_head->num_partition_entries); i++) {
		/* Stop at the first non valid PTE */
		if (!_is_pte_valid(&pgpt_pte[i]))
			break;

		printf("%3d\t0x%08llx\t0x%08llx\t\"%s\"\n", (i + 1),
		       le64_to_cpu(pgpt_pte[i].starting_lba),
		       le64_to_cpu(pgpt_pte[i].ending_lba),
		       _print_efiname(&pgpt_pte[i]));
		printf("\tattrs:\t0x%016llx\n", pgpt_pte[i].attributes.raw);
		uuid_bin = (unsigned char *)pgpt_pte[i].partition_type_guid.b;
		uuid_bin_to_str(uuid_bin, uuid, UUID_STR_FORMAT_GUID);
		printf("\ttype:\t%s\n", uuid);
		if (CONFIG_IS_ENABLED(PARTITION_TYPE_GUID)) {
			const char *type = uuid_guid_get_str(uuid_bin);

			if (type)
				printf("\ttype:\t%s\n", type);
		}
		uuid_bin = (unsigned char *)pgpt_pte[i].unique_partition_guid.b;
		uuid_bin_to_str(uuid_bin, uuid, UUID_STR_FORMAT_GUID);
		printf("\tguid:\t%s\n", uuid);
	}
}

static int sf_partition_parse(struct blk_desc *desc, struct spi_flash *flash)
{
	int ret = 0, i = 0;
	struct disk_partition disk_partition;
	struct partitions *part;

	if (!desc || !flash) {
		pr_err("Inval args\n");
		return -EINVAL;
	}

	part = p_sf_ept->partitions;
	for (i = 1; i < MAX_PART_COUNT; i++) {
		memset(&disk_partition, 0, sizeof(struct disk_partition));
		ret = _part_get_info_efi(desc, flash, i, &disk_partition);
		if (ret)
			break;
		pr_debug("part_name = %s, offset = 0x%lx, size = 0x%lx\n",
			 disk_partition.name,
			 disk_partition.start,
			 disk_partition.size);

		strncpy(part->name, (char *)disk_partition.name,
			strlen((char *)disk_partition.name));
		part->num = i;
		part->offset = disk_partition.start * desc->blksz;
		part->size = disk_partition.size * desc->blksz;
		part++;
	}
	p_sf_ept->count = i - 1;
	pr_debug("sf partition count = %d\n", p_sf_ept->count);

	return 0;
}

int sf_device_init(struct spi_flash *flash)
{
	int ret = 0;
	struct blk_desc sf_desc;

	if (!p_sf_ept) {
		ret = _zalloc_iptbl(&p_sf_ept);
		if (ret) {
			pr_err("sf partition cannot allocate memory\n");
			return -ENOMEM;
		}
	} else {
		p_sf_ept->count = 0;
		memset(p_sf_ept->partitions, 0,
		       sizeof(struct partitions) * MAX_PART_COUNT);
	}

	if (!part_buf) {
		part_buf = memalign(8, sizeof(u8) * OSPI_PTN_SZ);
		if (!part_buf) {
			pr_err("sf partition cannot allocate memory\n");
			return -ENOMEM;
		}
	}
	memset(part_buf, 0, sizeof(u8) * OSPI_PTN_SZ);

	sf_desc.blksz = OSPI_BLK_SZ;
	sf_desc.lba = (flash->size - OSPI_PTN_OFF) / sf_desc.blksz - 1;
	ret = sf_partition_parse(&sf_desc, flash);
	if (ret) {
		pr_err("sf gpt partition parse fail\n");
		return -EINVAL;
	}

	ret = _is_valid_gpt_buf(&sf_desc, part_buf);
	if (ret) {
		pr_err("check gpt buf fail\n");
		return -EINVAL;
	}

	return 0;
}

