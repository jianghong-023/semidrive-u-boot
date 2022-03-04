// SPDX-License-Identifier: GPL-2.0+
/*
 * SDHCI ADMA2 helper functions.
 */

#include <common.h>
#include <cpu_func.h>
#include <sdhci.h>
#include <malloc.h>
#include <asm/cache.h>

static void sdhci_adma_desc(struct sdhci_adma_desc *desc,
			    dma_addr_t addr, u16 len, bool end)
{
	u8 attr;

	attr = ADMA_DESC_ATTR_VALID | ADMA_DESC_TRANSFER_DATA;
	if (end)
		attr |= ADMA_DESC_ATTR_END;

	desc->attr = attr;
	desc->len = len;
	desc->reserved = 0;
	desc->addr_lo = lower_32_bits(addr);
#ifdef CONFIG_DMA_ADDR_T_64BIT
	desc->addr_hi = upper_32_bits(addr);
#endif
}

#ifdef CONFIG_MMC_SDHCI_DWCMSHC
#define ADMA_BOUNDARY_SIZE (0x8000000)
#define CALC_BOUNDARY(x, y) \
	({ \
		typeof(x) _x = x; \
		typeof(y) _y = y; \
		(_x + _y) & ~(_y - 1); \
	})

#define FV_ADMA2_ATTR_LEN(v) \
	({ \
		typeof(v) _v = v; \
		((_v & 0xffff) << 16) | ((_v & 0x3ff0000) >> 10); \
	})

static void sdhci_adma64_desc(struct sdhci_adma64_desc *desc,
			    dma_addr_t addr, u32 len, bool end)
{
	u8 attr;

	attr = ADMA_DESC_ATTR_VALID;
	if (end)
		attr |= ADMA_DESC_ATTR_END;
	else
		attr |= ADMA_DESC_TRANSFER_DATA;

	desc->attr = attr | FV_ADMA2_ATTR_LEN(len);;
	desc->addr_lo = lower_32_bits(addr);
#ifdef CONFIG_DMA_ADDR_T_64BIT
	desc->addr_hi = upper_32_bits(addr);
#endif
}

void sdhci_prepare_adma2_table(struct sdhci_adma64_desc *table,
			      struct mmc_data *data, dma_addr_t addr)
{
	uint trans_bytes = data->blocksize * data->blocks;
	struct sdhci_adma64_desc *desc = table;
	struct sdhci_adma64_desc *desc_bak = table;
	uint len = 0, offset = 0;
	dma_addr_t boundary_addr;
	u32 desc_cnt = 0;
	int i = 0;

	while (trans_bytes) {
		if (trans_bytes > ADMA2_V4_MAX_LEN)
			len = ADMA2_V4_MAX_LEN;
		else
			len = trans_bytes;

		boundary_addr = CALC_BOUNDARY(addr, ADMA_BOUNDARY_SIZE);
		if ((addr + len) > boundary_addr) {
			offset = boundary_addr - addr;
			sdhci_adma64_desc(desc, addr, offset, false);
			addr += offset;
			len -= offset;
			trans_bytes -= offset;
			desc++;
			desc_cnt++;
		}
		sdhci_adma64_desc(desc, addr, len, false);

		addr += len;
		trans_bytes -= len;
		desc++;
		desc_cnt++;
	}

	sdhci_adma64_desc(desc, 0, 0, true);
	desc_cnt++;

	for (i = 0; i < desc_cnt; i++) {
		pr_debug("%d: desc_addr_h = %x, desc_addr_l = %x, desc_attr = %x\n",
				i, desc_bak->addr_hi, desc_bak->addr_lo, desc_bak->attr);
		desc_bak++;
	}
	flush_cache((dma_addr_t)table,
		    ROUND(desc_cnt * sizeof(struct sdhci_adma64_desc),
			  ARCH_DMA_MINALIGN));
}
#endif

/**
 * sdhci_prepare_adma_table() - Populate the ADMA table
 *
 * @table:	Pointer to the ADMA table
 * @data:	Pointer to MMC data
 * @addr:	DMA address to write to or read from
 *
 * Fill the ADMA table according to the MMC data to read from or write to the
 * given DMA address.
 * Please note, that the table size depends on CONFIG_SYS_MMC_MAX_BLK_COUNT and
 * we don't have to check for overflow.
 */
void sdhci_prepare_adma_table(struct sdhci_adma_desc *table,
			      struct mmc_data *data, dma_addr_t addr)
{
	uint trans_bytes = data->blocksize * data->blocks;
	uint desc_count = DIV_ROUND_UP(trans_bytes, ADMA_MAX_LEN);
	struct sdhci_adma_desc *desc = table;
	int i = desc_count;

	while (--i) {
		sdhci_adma_desc(desc, addr, ADMA_MAX_LEN, false);
		addr += ADMA_MAX_LEN;
		trans_bytes -= ADMA_MAX_LEN;
		desc++;
	}

	sdhci_adma_desc(desc, addr, trans_bytes, true);

	flush_cache((dma_addr_t)table,
		    ROUND(desc_count * sizeof(struct sdhci_adma_desc),
			  ARCH_DMA_MINALIGN));
}

/**
 * sdhci_adma_init() - initialize the ADMA descriptor table
 *
 * @return pointer to the allocated descriptor table or NULL in case of an
 * error.
 */
struct sdhci_adma_desc *sdhci_adma_init(void)
{
	return memalign(ARCH_DMA_MINALIGN, ADMA_TABLE_SZ);
}
