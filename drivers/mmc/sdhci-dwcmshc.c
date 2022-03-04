// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Synopsys DesignWare Cores Mobile Storage Host Controller
 *
 * Copyright (C) 2018 Synaptics Incorporated
 *
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <mmc.h>
#include <sdhci.h>
#include <errno.h>
#include "mmc_private.h"

#define MAX_TUNING_LOOP 140

#define RSTGEN_EMMC1_ADDR 0x3844e000
#define RSTGEN_EMMC2_ADDR 0x3844f000
#define RSTGEN_EMMC3_ADDR 0x38450000
#define RSTGEN_EMMC4_ADDR 0x38451000

#define SCR_SEC_BASE 0x38200000
#define SCR_MSHC1_DDR_MODE 0x10
#define SCR_MSHC2_DDR_MODE 0x14
#define SCR_MSHC3_DDR_MODE 0x18
#define SCR_MSHC4_DDR_MODE 0x1c

#define CKGEN_EMMC1_IP_SLICE_ADDR 0x38009000
#define CKGEN_EMMC2_IP_SLICE_ADDR 0x3800a000
#define CKGEN_EMMC3_IP_SLICE_ADDR 0x3800b000
#define CKGEN_EMMC4_IP_SLICE_ADDR 0x3800c000
#define CKGEN_EMMC1_LP_GATING_ADDR 0x3810c000
#define CKGEN_EMMC2_LP_GATING_ADDR 0x3810d000
#define CKGEN_EMMC3_LP_GATING_ADDR 0x3810e000
#define CKGEN_EMMC4_LP_GATING_ADDR 0x3810f000
#define CLKGEN_IP_SLICE_CTL_CG_EN_MASK (1 << 0)
#define CLKGEN_IP_SLICE_CTL_CLK_SRC_SEL_MASK (0x7 << 1)
#define CLKGEN_IP_SLICE_CTL_CLK_SRC_SEL(x) ((x & 0x7) << 1)
#define CLKGEN_IP_SLICE_CTL_PRE_DIV_NUM_MASK (0x7 << 4)
#define CLKGEN_IP_SLICE_CTL_PRE_DIV_NUM(x) ((x & 0x7) << 4)
#define CLKGEN_IP_SLICE_CTL_POST_DIV_NUM_MASK (0x3f << 10)
#define CLKGEN_IP_SLICE_CTL_POST_DIV_NUM(x) ((x & 0x3f) << 10)

#define CLKGEN_PRE_DIV_NUM_MAX 0x7
#define CLKGEN_POST_DIV_NUM_MAX 0x3F

#define SDHCI_VENDOR_BASE_REG (0xE8)

#define SDHCI_VENDER_EMMC_CTRL_REG (0x2C)
#define SDHCI_IS_EMMC_CARD_MASK BIT(0)

#define SDHCI_VENDER_AT_CTRL_REG (0x40)
#define SDHCI_TUNE_CLK_STOP_EN_MASK BIT(16)
#define SDHCI_TUNE_SWIN_TH_VAL_LSB (24)
#define SDHCI_TUNE_SWIN_TH_VAL_MASK (0xFF)

#define DWC_MSHC_PTR_PHY_REGS 0x300
#define DWC_MSHC_PHY_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x0)
#define PAD_SN_LSB 20
#define PAD_SN_MASK 0xF
#define PAD_SN_DEFAULT ((0x8 & PAD_SN_MASK) << PAD_SN_LSB)
#define PAD_SP_LSB 16
#define PAD_SP_MASK 0xF
#define PAD_SP_DEFAULT ((0x9 & PAD_SP_MASK) << PAD_SP_LSB)
#define PHY_PWRGOOD BIT(1)
#define PHY_RSTN BIT(0)

#define DWC_MSHC_CMDPAD_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x4)
#define DWC_MSHC_DATPAD_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x6)
#define DWC_MSHC_CLKPAD_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x8)
#define DWC_MSHC_STBPAD_CNFG (DWC_MSHC_PTR_PHY_REGS + 0xA)
#define DWC_MSHC_RSTNPAD_CNFG (DWC_MSHC_PTR_PHY_REGS + 0xC)
#define TXSLEW_N_LSB 9
#define TXSLEW_N_MASK 0xF
#define TXSLEW_P_LSB 5
#define TXSLEW_P_MASK 0xF
#define WEAKPULL_EN_LSB 3
#define WEAKPULL_EN_MASK 0x3
#define RXSEL_LSB 0
#define RXSEL_MASK 0x3

#define DWC_MSHC_COMMDL_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x1C)
#define DWC_MSHC_SDCLKDL_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x1D)
#define DWC_MSHC_SDCLKDL_DC (DWC_MSHC_PTR_PHY_REGS + 0x1E)
#define DWC_MSHC_SMPLDL_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x20)
#define DWC_MSHC_ATDL_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x21)

#define DWC_MSHC_DLL_CTRL (DWC_MSHC_PTR_PHY_REGS + 0x24)
#define DWC_MSHC_DLL_CNFG1 (DWC_MSHC_PTR_PHY_REGS + 0x25)
#define DWC_MSHC_DLL_CNFG2 (DWC_MSHC_PTR_PHY_REGS + 0x26)
#define DWC_MSHC_DLLDL_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x28)
#define DWC_MSHC_DLL_OFFSET (DWC_MSHC_PTR_PHY_REGS + 0x29)
#define DWC_MSHC_DLLLBT_CNFG (DWC_MSHC_PTR_PHY_REGS + 0x2C)
#define DWC_MSHC_DLL_STATUS (DWC_MSHC_PTR_PHY_REGS + 0x2E)
#define ERROR_STS BIT(1)
#define LOCK_STS BIT(0)

#define DWC_MSHC_PHY_PAD_SD_CLK                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (0 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_SD_DAT                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_SD_STB                                                \
	((1 << TXSLEW_N_LSB) | (3 << TXSLEW_P_LSB) | (2 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))

#define DWC_MSHC_PHY_PAD_EMMC_CLK                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (0 << WEAKPULL_EN_LSB) |  \
	 (0 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_EMMC_DAT                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (1 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))
#define DWC_MSHC_PHY_PAD_EMMC_STB                                              \
	((2 << TXSLEW_N_LSB) | (2 << TXSLEW_P_LSB) | (2 << WEAKPULL_EN_LSB) |  \
	 (1 << RXSEL_LSB))

static struct dm_mmc_ops sdhci_dwcmshc_mmc_ops;

static void dwcmshc_phy_pad_config(struct sdhci_host *host)
{
	u16 clk_ctrl;
	struct sdhci_dwcmshc_plat *plat = host->plat;

	/* Disable the card clock */
	clk_ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	if (plat->card_is_emmc) {
		sdhci_writew(host, DWC_MSHC_PHY_PAD_EMMC_DAT, DWC_MSHC_CMDPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_EMMC_DAT, DWC_MSHC_DATPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_EMMC_CLK, DWC_MSHC_CLKPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_EMMC_STB, DWC_MSHC_STBPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_EMMC_DAT, DWC_MSHC_RSTNPAD_CNFG);
	} else {
		sdhci_writew(host, DWC_MSHC_PHY_PAD_SD_DAT, DWC_MSHC_CMDPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_SD_DAT, DWC_MSHC_DATPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_SD_CLK, DWC_MSHC_CLKPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_SD_STB, DWC_MSHC_STBPAD_CNFG);
		sdhci_writew(host, DWC_MSHC_PHY_PAD_SD_DAT, DWC_MSHC_RSTNPAD_CNFG);
	}

	return;
}

static inline void dwcmshc_phy_delay_config(struct sdhci_host *host)
{
	sdhci_writeb(host, 1, DWC_MSHC_COMMDL_CNFG);
	sdhci_writeb(host, 0, DWC_MSHC_SDCLKDL_CNFG);
	sdhci_writeb(host, 8, DWC_MSHC_SMPLDL_CNFG);
	sdhci_writeb(host, 8, DWC_MSHC_ATDL_CNFG);

	return;
}

static int dwcmshc_phy_dll_config(struct sdhci_host *host)
{
	u16 clk_ctrl;
	u32 reg;
	unsigned int timeout = 150;

	sdhci_writeb(host, 0, DWC_MSHC_DLL_CTRL);

	/* Disable the card clock */
	clk_ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	dwcmshc_phy_delay_config(host);

	sdhci_writeb(host, 0x20, DWC_MSHC_DLL_CNFG1);
	// TODO: set the dll value by real chip
	sdhci_writeb(host, 0x0, DWC_MSHC_DLL_CNFG2);
	sdhci_writeb(host, 0x60, DWC_MSHC_DLLDL_CNFG);
	sdhci_writeb(host, 0x0, DWC_MSHC_DLL_OFFSET);
	sdhci_writew(host, 0x0, DWC_MSHC_DLLLBT_CNFG);

	/* Enable the clock */
	clk_ctrl |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	sdhci_writeb(host, 1, DWC_MSHC_DLL_CTRL);

	/* Wait max 150 ms */
	while (1) {
		reg = sdhci_readb(host, DWC_MSHC_DLL_STATUS);
		if (reg & LOCK_STS)
			break;
		if (!timeout) {
			pr_err("%s: dwcmshc wait for phy dll lock timeout!\n",
				   host->name);
			return -1;
		}
		timeout--;
		mdelay(1);
	}

	reg = sdhci_readb(host, DWC_MSHC_DLL_STATUS);
	if (reg & ERROR_STS)
		return -1;

	//hs400 clr tuned_clk bit, or rx err
	reg = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	reg &= ~(SDHCI_CTRL_TUNED_CLK);
	sdhci_writew(host, reg, SDHCI_HOST_CONTROL2);

	return 0;
}

static void dwcmshc_phy_reset(struct sdhci_host *host)
{
	/* reset phy */
	sdhci_writew(host, 0, DWC_MSHC_PHY_CNFG);
}

static int dwcmshc_phy_init(struct sdhci_host *host)
{
	u32 reg;
	unsigned int timeout = 150;

	/* reset phy */
	sdhci_writew(host, 0, DWC_MSHC_PHY_CNFG);

	/* Disable the clock */
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	dwcmshc_phy_pad_config(host);
	dwcmshc_phy_delay_config(host);

	/* Wait max 150 ms */
	while (1) {
		reg = sdhci_readl(host, DWC_MSHC_PHY_CNFG);
		if (reg & PHY_PWRGOOD)
			break;
		if (!timeout) {
			pr_err("%s: dwcmshc wait for phy power good timeout!\n",
				   host->name);
			return -1;
		}
		timeout--;
		mdelay(1);
	}

	reg = PAD_SN_DEFAULT | PAD_SP_DEFAULT;
	sdhci_writel(host, reg, DWC_MSHC_PHY_CNFG);

	/* de-assert the phy */
	reg |= PHY_RSTN;
	sdhci_writel(host, reg, DWC_MSHC_PHY_CNFG);

	return 0;
}

static void emmc_card_init(struct sdhci_host *host)
{
	u16 reg;
	u16 vender_base;
	struct sdhci_dwcmshc_plat *plat = host->plat;

	/* read verder base register address */
	vender_base = sdhci_readw(host, SDHCI_VENDOR_BASE_REG) & 0xFFF;

	reg = sdhci_readw(host, vender_base + SDHCI_VENDER_EMMC_CTRL_REG);
	reg &= ~SDHCI_IS_EMMC_CARD_MASK;
	reg |= plat->card_is_emmc;
	sdhci_writew(host, reg, vender_base + SDHCI_VENDER_EMMC_CTRL_REG);
}

static inline void dwcmshc_auto_tuning_set(struct sdhci_host *host)
{
	u32 reg;
	u16 vender_base;

	/* read verder base register address */
	vender_base = sdhci_readw(host, SDHCI_VENDOR_BASE_REG) & 0xFFF;

	reg = sdhci_readl(host, vender_base + SDHCI_VENDER_AT_CTRL_REG);
	reg &= ~(SDHCI_TUNE_SWIN_TH_VAL_MASK << SDHCI_TUNE_SWIN_TH_VAL_LSB);
	reg |= (0xF << SDHCI_TUNE_SWIN_TH_VAL_LSB);
	reg |= SDHCI_TUNE_CLK_STOP_EN_MASK;
	sdhci_writel(host, reg, vender_base + SDHCI_VENDER_AT_CTRL_REG);
}

static int clkgen_ip_slice_set(struct sdhci_host *host, struct clkgen_app_ip_cfg *ip_cfg)
{
	uint32_t reg_read = 0;
	uint32_t reg_write = 0;
	void __iomem *ip_slice_base_addr;
	uint32_t reg = 0, ip_slice;
	unsigned long timeout;
	int ret = 0;

	switch (host->plat->id) {
	case MSHC1:
		ip_slice = CKGEN_EMMC1_IP_SLICE_ADDR;
		break;
	case MSHC2:
		ip_slice = CKGEN_EMMC2_IP_SLICE_ADDR;
		break;
	case MSHC3:
		ip_slice = CKGEN_EMMC3_IP_SLICE_ADDR;
		break;
	case MSHC4:
		ip_slice= CKGEN_EMMC4_IP_SLICE_ADDR;
		break;
	}

	ip_slice_base_addr = ioremap(ip_slice, 32);
	if (IS_ERR(ip_slice_base_addr)) {
		return PTR_ERR(ip_slice_base_addr);
	}

	//clear pre_en
	reg_read = readl(ip_slice_base_addr);

	if ((reg_read & CLKGEN_IP_SLICE_CTL_CG_EN_MASK) != 0) {
		reg_write = reg_read & (~CLKGEN_IP_SLICE_CTL_CG_EN_MASK);
		writel(reg_write, ip_slice_base_addr);
		/* Wait max 100 ms */
		timeout = 100;
		while (timeout) {
			reg = readl(ip_slice_base_addr);
			if ((reg >> 28) & 0x1)
				break;
			timeout--;
			udelay(1000);
			if (!timeout) {
				pr_err("emmc set clk timeout\n");
				ret = -1;
				goto out;
			}
		}
	}

	//select clk src
	reg_read = readl(ip_slice_base_addr);
	reg_write = (reg_read & (~CLKGEN_IP_SLICE_CTL_CLK_SRC_SEL_MASK)) |
		CLKGEN_IP_SLICE_CTL_CLK_SRC_SEL(ip_cfg->clk_src_sel_num);
	writel(reg_write, ip_slice_base_addr);
	//set pre_en
	reg_read = readl(ip_slice_base_addr);
	reg_write = reg_read | CLKGEN_IP_SLICE_CTL_CG_EN_MASK;
	writel(reg_write, ip_slice_base_addr);
	/* Wait max 100 ms */
	timeout = 100;
	while (timeout) {
		reg = readl(ip_slice_base_addr);
		if ((reg >> 28) & 0x1)
			break;
		timeout--;
		udelay(1000);
		if (!timeout) {
			pr_err("emmc set clk timeout\n");
			ret = -1;
			goto out;
		}
	}

	//set pre_div
	reg_read = readl(ip_slice_base_addr);
	reg_write = (reg_read & (~CLKGEN_IP_SLICE_CTL_PRE_DIV_NUM_MASK)) |
		CLKGEN_IP_SLICE_CTL_PRE_DIV_NUM(ip_cfg->pre_div);
	writel(reg_write, ip_slice_base_addr);
	//wait pre_upd_ack is 0
	/* Wait max 100 ms */
	timeout = 100;
	while (timeout) {
		reg = readl(ip_slice_base_addr);
		if (!((reg >> 30) & 0x1))
			break;
		timeout--;
		udelay(1000);
		if (!timeout) {
			pr_err("emmc set clk timeout\n");
			ret = -1;
			goto out;
		}
	}

	//set post_div
	reg_read = readl(ip_slice_base_addr);
	reg_write = (reg_read & (~CLKGEN_IP_SLICE_CTL_POST_DIV_NUM_MASK)) |
		CLKGEN_IP_SLICE_CTL_POST_DIV_NUM(ip_cfg->post_div);
	writel(reg_write, ip_slice_base_addr);
	//wait post_upd_ack is 0
	/* Wait max 100 ms */
	timeout = 100;
	while (timeout) {
		reg = readl(ip_slice_base_addr);
		if (!((reg >> 31) & 0x1))
			break;
		timeout--;
		udelay(1000);
		if (!timeout) {
			pr_err("emmc set clk timeout\n");
			ret = -1;
			goto out;
		}
	}

out:
	iounmap(ip_slice_base_addr);
	return ret;
}

static int clkgen_lp_gating_en(struct sdhci_host *host, unsigned int enable)
{
	uint32_t reg_read = 0;
	uint32_t reg_write = 0;
	void __iomem *lp_gating_base_addr;
	uint32_t lp_gating;

	switch (host->plat->id) {
	case MSHC1:
		lp_gating = CKGEN_EMMC1_LP_GATING_ADDR;
		break;
	case MSHC2:
		lp_gating = CKGEN_EMMC2_LP_GATING_ADDR;
		break;
	case MSHC3:
		lp_gating = CKGEN_EMMC3_LP_GATING_ADDR;
		break;
	case MSHC4:
		lp_gating = CKGEN_EMMC4_LP_GATING_ADDR;
		break;
	}

	lp_gating_base_addr = ioremap(lp_gating, 32);
	if (IS_ERR(lp_gating_base_addr)) {
		return PTR_ERR(lp_gating_base_addr);
	}

	reg_read = readl(lp_gating_base_addr);
	reg_write = reg_read & ~(1 << 31);
	writel(reg_write, lp_gating_base_addr);

	reg_read = readl(lp_gating_base_addr);
	reg_write = reg_read & ~(1 << 31);
	if (!enable)
		reg_write |= (1 << 1);

	writel(reg_write, lp_gating_base_addr);
	iounmap(lp_gating_base_addr);

	return 0;
}

static int mshc_clkgen_config(struct sdhci_host *host, unsigned long freq)
{
	int ret = 0;
	struct clkgen_app_ip_cfg ip_cfg;
	ip_cfg.clk_src_sel_num = 4;
	// TODO: hardcode mshc clock source 400Mhz
	const uint32_t mshc_base_freq = 400000000;
	uint32_t clock_div;
	uint32_t pre_div, post_div;
	uint32_t clock_div_succ = 0;
	uint32_t freq_bias = mshc_base_freq;
	uint32_t curr_freq_bias;

	clock_div = DIV_ROUND_UP(mshc_base_freq, freq);

	for (int i = 0; i <= CLKGEN_PRE_DIV_NUM_MAX; i++) {
		pre_div = i;
		post_div = DIV_ROUND_UP(clock_div, i + 1) - 1;

		if (post_div <= CLKGEN_POST_DIV_NUM_MAX) {
			if (0 == clock_div % (pre_div + 1)) {
				ip_cfg.pre_div = pre_div;
				ip_cfg.post_div = post_div;
				clock_div_succ = 1;
				break;
			}

			curr_freq_bias = abs((pre_div + 1) * (post_div + 1) - clock_div);
			if (curr_freq_bias < freq_bias) {
				freq_bias = curr_freq_bias;
				ip_cfg.pre_div = pre_div;
				ip_cfg.post_div = post_div;
				clock_div_succ = 1;
			}
		}
	}
	pr_debug("pre_div = %x, post_div = %x\n", ip_cfg.pre_div, ip_cfg.post_div);

	if (0 == clock_div_succ) {
		pr_err("%s: calculate the clock div failed!\n", __func__);
		ret = -1;
		goto out;
	}

	ret = clkgen_ip_slice_set(host, &ip_cfg);
	if (ret) {
		pr_err("%s: clkgen set ip clock failed\n", __func__);
		goto out;
	}

	/*enable mmc host clock*/
	ret = clkgen_lp_gating_en(host, 1);
	if (ret) {
		pr_err("%s: clkgen enable ip clock failed\n", __func__);
		goto out;
	}

out:
	return ret;
}

void dwcmshc_set_clock(struct sdhci_host *host, unsigned long clock)
{
	host->mmc->clock = 0;
	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	/*
	 * Beacuse the clock will be 2 dvider by mshc model,
	 * so we need twice base frequency.
	 */
	if (host->quirks & SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN) {
		if (!mshc_clkgen_config(host, clock * 2))
			host->mmc->clock = clock;
	} else {
		host->mmc->clock = clock;
	}
	pr_debug("%s: Set clock = %ld, actual clock = %d\n",
		host->name, clock, host->mmc->clock);
}

static int set_ddr_mode(struct sdhci_host *host, unsigned int ddr_mode)
{
	struct sdhci_dwcmshc_plat *plat = host->plat;
	void __iomem *ioaddr;
	u32 reg = 0, ddr_mode_addr;

	switch (host->plat->id) {
	case MSHC1:
		ddr_mode_addr = SCR_MSHC1_DDR_MODE;
		break;
	case MSHC2:
		ddr_mode_addr = SCR_MSHC2_DDR_MODE;
		break;
	case MSHC3:
		ddr_mode_addr = SCR_MSHC3_DDR_MODE;
		break;
	case MSHC4:
		ddr_mode_addr = SCR_MSHC4_DDR_MODE;
		break;
	}

	ioaddr = ioremap(SCR_SEC_BASE + (ddr_mode_addr << 10), 32);
	if (IS_ERR(ioaddr))
		return PTR_ERR(ioaddr);

	if (plat->scr_signals_ddr) {
		reg = readl(ioaddr);
		reg &= ~(0x1);
		reg |= ddr_mode;
		writel(reg, ioaddr);
	}

	iounmap(ioaddr);
	return 0;
}

static void dwcmshc_set_ddr(struct sdhci_host *host)
{
	u32 ddr_mode = 0;
	struct mmc *mmc = host->mmc;

	if ((mmc->selected_mode == UHS_DDR50)
			|| (mmc->selected_mode == MMC_DDR_52)
			|| (mmc->selected_mode == MMC_HS_400)
			|| (mmc->selected_mode == MMC_HS_400_ES))
		ddr_mode = 1;
	set_ddr_mode(host, ddr_mode);

	if ((mmc->selected_mode > MMC_HS_200) &&
			(host->mmc->clock > 100000000)) {
		if (dwcmshc_phy_dll_config(host))
			pr_err("%s: phy dll config failed!\n", host->name);
	}
}

void _sdhci_reset(struct sdhci_host *host, u8 mask)
{
	unsigned long timeout;

	/* Wait max 100 ms */
	timeout = 100;
	sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET);
	while (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & mask) {
		if (timeout == 0) {
			pr_err("%s: Reset 0x%x never completed.\n",
			       __func__, (int)mask);
			return;
		}
		timeout--;
		udelay(1000);
	}
}


static void dwcmshc_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	struct mmc *mmc = host->mmc;

	if (!sdhci_dwcmshc_mmc_ops.get_cd(mmc->dev))
		return;

	if (mask & SDHCI_RESET_ALL) {
		/* Hold phy reset when software reset host. */
		dwcmshc_phy_reset(host);
	}

	_sdhci_reset(host, mask);

	if (mask & SDHCI_RESET_ALL) {
		emmc_card_init(host);
	}
}

int dwcmshc_set_ios_post(struct sdhci_host *host)
{
	u32 phy_cnfg_reg;

	sdhci_set_control_reg(host);

	/* When switch signal voltage success, need init mshc phy. */
	phy_cnfg_reg = sdhci_readw(host, DWC_MSHC_PHY_CNFG);
	if ((phy_cnfg_reg & PHY_RSTN) == 0u) {
		dwcmshc_phy_init(host);
	}

	dwcmshc_set_ddr(host);

	return 0;
}

static void dwcmshc_get_property(struct udevice *dev)
{
	u32 scr_signal;
	struct sdhci_dwcmshc_plat *plat = dev_get_plat(dev);

	dev_read_u32(dev, "card_id", &plat->id);

	if (dev_read_bool(dev, "card-is-emmc"))
		plat->card_is_emmc = 1;
	else
		plat->card_is_emmc = 0;

	if (!dev_read_u32(dev, "sdrv,scr_signals_ddr", &scr_signal))
		plat->scr_signals_ddr = scr_signal;
	pr_info("the ddr mode scr signal = %d\n", plat->scr_signals_ddr);

}

void sdhci_start_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl |= SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	/*
	 * As per the Host Controller spec v3.00, tuning command
	 * generates Buffer Read Ready interrupt, so enable that.
	 *
	 * Note: The spec clearly says that when tuning sequence
	 * is being performed, the controller does not generate
	 * interrupts other than Buffer Read Ready interrupt. But
	 * to make sure we don't hit a controller bug, we _only_
	 * enable Buffer Read Ready interrupt here.
	 */
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_SIGNAL_ENABLE);
}

void sdhci_end_tuning(struct sdhci_host *host)
{
	sdhci_writel(host, SDHCI_INT_DATA_MASK | SDHCI_INT_CMD_MASK,
		     SDHCI_INT_ENABLE);
	sdhci_writel(host, 0x0, SDHCI_SIGNAL_ENABLE);
}

int sdhci_stop_cmd(struct sdhci_host *host, u32 opcode)
{
	struct mmc *mmc = host->mmc;
	struct mmc_cmd cmd = {};
	int ret = 0;

	/*
	 * eMMC specification specifies that CMD12 can be used to stop a tuning
	 * command, but SD specification does not, so do nothing unless it is
	 * eMMC.
	 */
	if (opcode != MMC_CMD_SEND_TUNING_BLOCK_HS200)
		return 0;

	cmd.cmdidx = MMC_CMD_STOP_TRANSMISSION;
	cmd.resp_type = MMC_RSP_R1;

	ret = mmc_send_cmd(mmc, &cmd, NULL);
	wmb();

	return ret;
}

void sdhci_reset_tuning(struct sdhci_host *host)
{
	u16 ctrl;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl &= ~SDHCI_CTRL_TUNED_CLK;
	ctrl &= ~SDHCI_CTRL_EXEC_TUNING;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
}

void sdhci_abort_tuning(struct sdhci_host *host, u32 opcode)
{
	sdhci_reset_tuning(host);

	dwcmshc_sdhci_reset(host, SDHCI_RESET_CMD);
	dwcmshc_sdhci_reset(host, SDHCI_RESET_DATA);

	sdhci_end_tuning(host);

	sdhci_stop_cmd(host, opcode);
}

/*
 * We use sdhci_send_tuning() because mmc_send_tuning() is not a good fit. SDHCI
 * tuning command does not have a data payload (or rather the hardware does it
 * automatically) so mmc_send_tuning() will return -EIO. Also the tuning command
 * interrupt setup is different to other commands and there is no timeout
 * interrupt so special handling is needed.
 */
int sdhci_send_tuning(struct sdhci_host *host, u32 opcode)
{
	struct mmc *mmc = host->mmc;
	struct mmc_cmd cmd = {};
	int ret = 0;

	cmd.cmdidx = opcode;
	cmd.resp_type = MMC_RSP_R1;

	/*
	 * In response to CMD19, the card sends 64 bytes of tuning
	 * block to the Host Controller. So we set the block size
	 * to 64 here.
	 */
	if (cmd.cmdidx == MMC_CMD_SEND_TUNING_BLOCK_HS200 &&
	    mmc->bus_width == 8)
		sdhci_writew(host, 128, SDHCI_BLOCK_SIZE);
	else
		sdhci_writew(host, 64, SDHCI_BLOCK_SIZE);

	/*
	 * The tuning block is sent by the card to the host controller.
	 * So we set the TRNS_READ bit in the Transfer Mode register.
	 * This also takes care of setting DMA Enable and Multi Block
	 * Select in the same register to 0.
	 */
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

	ret = mmc_send_cmd(mmc, &cmd, NULL);

	wmb();
	return ret;
}

static int __dwcmshc_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int i, ret;
	struct sdhci_dwcmshc_plat *plat = host->plat;
	u16 ctrl;

	/*
	 * Issue opcode repeatedly till Execute Tuning is set to 0 or the number
	 * of loops reaches tuning loop count.
	 */
	for (i = 0; i < MAX_TUNING_LOOP; i++) {
		ret = sdhci_send_tuning(host, opcode);
		if (ret) {
			pr_err("%s: Tuning timeout, falling back to fixed sampling clock\n",
				host->name);
			sdhci_abort_tuning(host, opcode);
			return -ETIMEDOUT;
		}

		/* Spec does not require a delay between tuning cycles */
		if (plat->tuning_delay > 0)
			mdelay(plat->tuning_delay);

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
		if (!(ctrl & SDHCI_CTRL_EXEC_TUNING)) {
			if (ctrl & SDHCI_CTRL_TUNED_CLK) {
				pr_debug("sdhci: tuning cycle count = %d\n", i);
				if (i < 40)
					pr_err("SDHC: tuning cycle abnormal, num is %d!\n", i);
				return 0; /* Success! */
			}
			break;
		}
	}

	pr_err("%s: Tuning failed, falling back to fixed sampling clock\n",
		host->name);
	sdhci_reset_tuning(host);
	return -EAGAIN;
}

static int dwcmshc_execute_tuning(struct mmc *mmc, u8 opcode)
{
	u16 clk_ctrl;
	struct sdhci_host *host = mmc->priv;
	struct sdhci_dwcmshc_plat *plat = host->plat;

#if 0
	if (host->flags & SDHCI_HS400_TUNING)
		host->mmc->retune_period = 0;
#endif

	if ((plat->tuning_delay < 0) && (opcode == MMC_CMD_SEND_TUNING_BLOCK))
		plat->tuning_delay = 1;

	sdhci_reset_tuning(host);
	dwcmshc_auto_tuning_set(host);
	dwcmshc_sdhci_reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);

	/*
	 * For avoid giltches, need disable the card clock before set
	 * EXEC_TUNING bit.
	 */
	clk_ctrl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	sdhci_start_tuning(host);
	host->plat->tuning_in_progress = 1;

	/* enable the card clock */
	clk_ctrl |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl, SDHCI_CLOCK_CONTROL);

	plat->tuning_err = __dwcmshc_execute_tuning(host, opcode);

	sdhci_end_tuning(host);
	host->plat->tuning_in_progress = 0;

	return 0;
}

static int mmc_reset(struct sdhci_host *host)
{
	int ret = 0;
	unsigned long timeout;
	void __iomem *ioaddr;
	u32 reg = 0, rstgen_addr;

	switch (host->plat->id) {
	case MSHC1:
		rstgen_addr = RSTGEN_EMMC1_ADDR;
		break;
	case MSHC2:
		rstgen_addr = RSTGEN_EMMC2_ADDR;
		break;
	case MSHC3:
		rstgen_addr = RSTGEN_EMMC3_ADDR;
		break;
	case MSHC4:
		rstgen_addr = RSTGEN_EMMC4_ADDR;
		break;
	}

	ioaddr = ioremap(rstgen_addr, 32);
	if (IS_ERR(ioaddr)) {
		return PTR_ERR(ioaddr);
	}

	reg = readl(ioaddr);
	if (reg & (1 << 31)) {
		reg &= ~(1 << 31);
		writel(reg, ioaddr);
	}

	if (!(reg & (1 << 1))) {
		reg &= ~(1 << 1);
		reg |= (1 << 1);
		writel(reg, ioaddr);
		/* Wait max 100 ms */
		timeout = 100;
		while (timeout) {
			reg = readl(ioaddr);
			if (reg & (1 << 1))
				break;
			timeout--;
			if (timeout == 0) {
				pr_err("%s %d: rstgen fail.\n", __func__, __LINE__);
				ret = -1;
				goto out;
			}
			udelay(1000);
		}
	}

	reg &= ~(0x1);
	writel(reg, ioaddr);
	/* Wait max 100 ms */
	timeout = 100;
	while (timeout) {
		reg = readl(ioaddr);
		if (reg & (1 << 30))
			break;
		timeout--;
		if (timeout == 0) {
			pr_err("%s %d: rstgen fail.\n", __func__, __LINE__);
			ret = -1;
			goto out;
		}
		udelay(1000);
	}

	reg |= 0x1;
	writel(reg, ioaddr);
	/* Wait max 100 ms */
	timeout = 100;
	while (timeout) {
		reg = readl(ioaddr);
		if (reg & (1 << 30))
			break;
		timeout--;
		if (timeout == 0) {
			pr_err("%s %d: rstgen fail.\n", __func__, __LINE__);
			ret = -1;
			goto out;
		}
		udelay(1000);
	}

out:
	iounmap(ioaddr);
	return ret;
}

static const struct sdhci_ops sdhci_dwcmshc_ops = {
	.set_clock		= dwcmshc_set_clock,
	.set_ios_post	= dwcmshc_set_ios_post,
	.platform_execute_tuning = dwcmshc_execute_tuning,
};

static int sdhci_dwcmshc_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct sdhci_dwcmshc_plat *plat = dev_get_plat(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	fdt_addr_t base;
	void __iomem *ioaddr;
	int ret;

	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ioaddr = devm_ioremap(dev, base, 0x10000);
	if (IS_ERR(ioaddr)) {
		ret = PTR_ERR(ioaddr);
		return -ENOMEM;
	}

	host->name = dev->name;
	host->ioaddr = ioaddr;
	host->ops = &sdhci_dwcmshc_ops;
	host->quirks |= SDHCI_QUIRK_WAIT_SEND_CMD
		| SDHCI_QUIRK_64BIT_DMA_ADDR
		| SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN;
	sdhci_dwcmshc_mmc_ops = sdhci_ops;

	ret = mmc_of_parse(dev, &plat->cfg);
	if (ret)
		return ret;

	plat->tuning_delay = -1;
	host->plat = plat;
	host->mmc = &plat->mmc;
	host->mmc->dev = dev;
	ret = sdhci_setup_cfg(&plat->cfg, host, 0, 400000);
	if (ret)
		return ret;

	upriv->mmc = &plat->mmc;
	host->mmc->priv = host;

	dwcmshc_get_property(dev);

	return sdhci_probe(dev);
}

static int sdhci_dwcmshc_bind(struct udevice *dev)
{
	struct sdhci_dwcmshc_plat *plat = dev_get_plat(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id sdhci_dwcmshc_match[] = {
	{ .compatible = "snps,dwcmshc-sdhci" },
	{}
};

U_BOOT_DRIVER(sdhci_dwcmshc) = {
	.name = "sdhci-dwcmshc",
	.id = UCLASS_MMC,
	.of_match = sdhci_dwcmshc_match,
	.bind = sdhci_dwcmshc_bind,
	.probe = sdhci_dwcmshc_probe,
	.priv_auto	= sizeof(struct sdhci_host),
	.plat_auto	= sizeof(struct sdhci_dwcmshc_plat),
	.ops = &sdhci_dwcmshc_mmc_ops,
};
