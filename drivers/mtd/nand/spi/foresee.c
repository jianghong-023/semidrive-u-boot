// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 exceet electronics GmbH
 *
 * Authors:
 *	Frieder Schrempf <frieder.schrempf@exceet.de>
 *	Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef __UBOOT__
#include <malloc.h>
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/bitops.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_FORESEE		0xCD

#define F35SQA001G_SI_MASK	GENMASK(5, 4)
#define F35SQA001G_SES_MASK	GENMASK(3, 0)
#define F35SQA001G_MAX_SES	4
static u64 f35sqa001g_ses_reg[] = {0x80, 0x84, 0x88, 0x8c};

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int f35sqa001g_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int f35sqa001g_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	/* Reserve 1 bytes for the BBM. */
	region->offset = (16 * section) + 1;
	region->length = 7;

	return 0;
}

static int f35sqa001g_ecc_get_ses(struct spinand_device *spinand,
				  u8 *status)
{
	int ret, i, num;
	u8 ses[4] = {0};
	struct spi_mem_op op[] = {
		SPINAND_GET_FEATURE_OP(f35sqa001g_ses_reg[0], &ses[0]),
		SPINAND_GET_FEATURE_OP(f35sqa001g_ses_reg[1], &ses[1]),
		SPINAND_GET_FEATURE_OP(f35sqa001g_ses_reg[2], &ses[2]),
		SPINAND_GET_FEATURE_OP(f35sqa001g_ses_reg[3], &ses[3]),
	};

	num = 0;
	for (i = 0; i < F35SQA001G_MAX_SES; i++) {
		ret = spi_mem_exec_op(spinand->slave, &op[i]);
		if (ret) {
			pr_err("%s: get sec_%d es fail\n", __func__, i);
			return ret;
		}
		if (ses[i] & F35SQA001G_SES_MASK == 0x1)
			num++;
		if (ses[i] & F35SQA001G_SES_MASK & 0x2)
			pr_debug("sec %d has more than 1-bit err\n", i);
	}

	*status = num;
	pr_debug("Detect %d sectors 1-bit err and be corrected\n", num);

	return 0;
}

static int f35sqa001g_ecc_get_status(struct spinand_device *spinand,
				     u8 status)
{
	int ret;
	u8 ses;

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_HAS_BITFLIPS:
		/*
		 * Read sector status register to determine a more fine grained
		 * bit error status
		 */
		ret = f35sqa001g_ecc_get_ses(spinand, &ses);
		if (ret)
			return ret;

		/*
		 * 1-bit err was detected in one or more sector and corrected
		 */
		/* bits sorted these regs Sector ECC Status 0~3 */
		return ses;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static const struct mtd_ooblayout_ops f35sqa001g_ooblayout = {
	.ecc = f35sqa001g_ooblayout_ecc,
	.rfree = f35sqa001g_ooblayout_free,
};

static const struct spinand_info foresee_spinand_table[] = {
	SPINAND_INFO("F35SQA001G", 0x71,
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&f35sqa001g_ooblayout,
				     f35sqa001g_ecc_get_status)),
};

/**
 * foresee_spinand_detect - initialize device related part in spinand_device
 * struct if it is a foresee device.
 * @spinand: SPI NAND device structure
 */
static int foresee_spinand_detect(struct spinand_device *spinand)
{
	u8 *id = spinand->id.data;
	int ret;

	/*
	 * FORESEE SPI NAND read ID need a dummy byte,
	 * so the first byte in raw_id is dummy.
	 */
	if (id[1] != SPINAND_MFR_FORESEE)
		return 0;

	ret = spinand_match_and_init(spinand, foresee_spinand_table,
				     ARRAY_SIZE(foresee_spinand_table), id[2]);
	if (ret)
		return ret;

	return 1;
}

static const struct spinand_manufacturer_ops foresee_spinand_manuf_ops = {
	.detect = foresee_spinand_detect,
};

const struct spinand_manufacturer foresee_spinand_manufacturer = {
	.id = SPINAND_MFR_FORESEE,
	.name = "Foresee",
	.ops = &foresee_spinand_manuf_ops,
};
