// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Semidrive Corporation
 * porting by shide.zhou@semidrive.com
 */

#include <clk.h>
#include <common.h>
#include <dm.h>
#include <reset.h>
#include <wdt.h>
#include <asm/io.h>
#include <asm/utils.h>
#include <linux/bitops.h>
#include <cpu_func.h>

DECLARE_GLOBAL_DATA_PTR;

#define SDRV_WDT_REBOOT_DELAY 0xffff
#define SDRV_WDT_PRE_DIV 1

/* wdt ctl */
#define WDG_CTRL_SOFT_RST_MASK		((unsigned) (1 << 0))
#define WDG_CTRL_SOFT_RST_SHIFT		(0U)
#define WDG_CTRL_SOFT_RST(x)		(((unsigned)(((unsigned)(x)) << WDG_CTRL_SOFT_RST_SHIFT)) & WDG_CTRL_SOFT_RST_MASK)

#define WDG_CTRL_WDG_EN_MASK		((unsigned) (1 << 1))

#define WDG_CTRL_CLK_SRC_MASK		(((unsigned) (1 << 2)) | ((unsigned) (1 << 3)) |((unsigned) (1 << 4)))

#define WDG_CTRL_WTC_SRC_MASK		((unsigned) (1 << 5))
#define WDG_CTRL_WTC_SRC_SHIFT		(5U)
#define WDG_CTRL_WTC_SRC(x)		(((unsigned)(((unsigned)(x)) << WDG_CTRL_WTC_SRC_SHIFT)) & WDG_CTRL_WTC_SRC_MASK)

#define WDG_CTRL_AUTO_RESTART_MASK	((unsigned) (1 << 6))

#define WDG_CTRL_WDG_EN_SRC_MASK	((unsigned) (1 << 8))
#define WDG_CTRL_WDG_EN_SRC_SHIFT	(8U)
#define WDG_CTRL_WDG_EN_SRC(x)		(((unsigned)(((unsigned)(x)) << WDG_CTRL_WDG_EN_SRC_SHIFT)) & WDG_CTRL_WDG_EN_SRC_MASK)

#define WDG_CTRL_WDG_EN_STA_MASK	((unsigned) (1 << 10))

#define WDG_CTRL_PRE_DIV_NUM_MASK	0xFFFF0000
#define WDG_CTRL_PRE_DIV_NUM_SHIFT	(16U)
#define WDG_CTRL_PRE_DIV_NUM(x)		(((unsigned)(((unsigned)(x)) << WDG_CTRL_PRE_DIV_NUM_SHIFT)) & WDG_CTRL_PRE_DIV_NUM_MASK)

/* wdt wrc ctl*/

#define WDG_WRC_CTRL_MODEM0_MASK	((unsigned) (1 << 0))
#define WDG_WRC_CTRL_MODEM1_MASK	((unsigned) (1 << 1))
#define WDG_WRC_CTRL_SEQ_REFR_MASK	((unsigned) (1 << 2))
#define WDG_WRC_CTRL_REFR_TRIG_MASK	((unsigned) (1 << 3))

/* wdt rst ctl */
#define WDG_RST_CTRL_RST_CNT_MASK	0x0000ffff
#define WDG_RST_CTRL_RST_CNT_SHIFT	(0U)
#define WDG_RST_CTRL_RST_CNT(x)		(((unsigned)(((unsigned)(x)) << WDG_RST_CTRL_RST_CNT_SHIFT)) & WDG_RST_CTRL_RST_CNT_MASK)
#define WDG_RST_CTRL_INT_RST_EN_MASK	((unsigned) (1 << 16))

#define WDG_RST_CTRL_INT_RST_MODE_MASK	((unsigned) (1 << 17))

/*RSTGEN_BOOT_REASON_REG:bit 0~3:bootreason, bit 4~7:wakeup source, bit 8~31: params */
#define BOOT_REASON_MASK	0xF

typedef enum {
	HALT_REASON_UNKNOWN = 0,
	HALT_REASON_POR,            // Cold-boot
	HALT_REASON_HW_WATCHDOG,    // HW watchdog timer
	HALT_REASON_LOWVOLTAGE,     // LV/Brownout condition
	HALT_REASON_HIGHVOLTAGE,    // High voltage condition.
	HALT_REASON_THERMAL,        // Thermal reason (probably overtemp)
	HALT_REASON_SW_RECOVERY,    // SW triggered reboot in order to enter recovery mode
	HALT_REASON_SW_RESET,       // Generic Software Initiated Reboot
	HALT_REASON_SW_WATCHDOG,    // Reboot triggered by a SW watchdog timer
	HALT_REASON_SW_PANIC,       // Reboot triggered by a SW panic or ASSERT
	HALT_REASON_SW_UPDATE,      // SW triggered reboot in order to begin firmware update
	HALT_REASON_SW_GLOBAL_POR,  // SW triggered global reboot
} platform_halt_reason;

struct halt_reason_desc {
	const char *desc;
	platform_halt_reason reason;
};

typedef enum wdg_clock_source {
	wdg_main_clk = 0,   // main clk, 3'b000
	wdg_bus_clk = 0x4,  // bus clk, 3'b001
	wdg_ext_clk = 0x8,  // ext clk, 3'b010
	wdg_lp_clk = 0x10   // lp clk, 3'b1xx
} wdg_clock_source_t;

struct sdrv_wdt_clk_struct {
	wdg_clock_source_t src;
	const char *name;
	int ration_to_ms;
};

struct semidrive_wdt_priv {
	void __iomem	*base;
	int dying_delay;
	unsigned int timeout;
	const struct sdrv_wdt_clk_struct *wdt_clk;
	unsigned clk_div;
	int sig_rstgen;
	unsigned int min_hw_heartbeat_ms;
	unsigned int max_hw_heartbeat_ms;
};

static const struct sdrv_wdt_clk_struct wdt_clk[4] = {
	{
		.src = wdg_main_clk,
		.name = "main-clk",
		.ration_to_ms = 24000, /* 24MHz */
	},
	{
		.src = wdg_bus_clk,
		.name = "bus-clk",
		.ration_to_ms = 250000, /* 250MHz */
	},
	{
		.src = wdg_ext_clk,
		.name = "external-clk",
		.ration_to_ms = 26000, /* 26MHz */
	},
	{
		.src = wdg_lp_clk,
		.name = "xtal-clk",
		.ration_to_ms= 32, /* 32KHz */
	},
};

static int semidrive_wdt_start(struct udevice *dev, u64 time_out, ulong flags)
{
	struct semidrive_wdt_priv *sdrv_wdt = dev_get_priv(dev);
	unsigned val, clk_div, wtc_con;
	unsigned int timeout;
	void __iomem *wdt_ctl = sdrv_wdt->base;
	void __iomem *wdt_wtc = wdt_ctl + 4;
	void __iomem *wdt_wrc_ctl = wdt_ctl + 8;
	void __iomem *wdt_wrc_val = wdt_ctl + 0xc;
	void __iomem *wdt_rst_ctl = wdt_ctl + 0x14;
	void __iomem *wdt_rst_int = wdt_ctl + 0x24;

	timeout = sdrv_wdt->timeout;

	/* set wdg_en_src in wdg_ctrl to 0x0 and clear wdg en src to 0x0 */
	writel(0, wdt_ctl);

	/* set wdg_en_src in wdg_ctrl to 0x1, select wdg src enable from register */
	val = readl(wdt_ctl);
	writel(val | WDG_CTRL_WDG_EN_SRC(1) | WDG_CTRL_WTC_SRC(1), wdt_ctl);

	/* wait until wdg_en_sta to 0x0 */
	while(readl(wdt_ctl) & WDG_CTRL_WDG_EN_STA_MASK);

	/* selcet clk src & div */
	val = readl(wdt_ctl);
	val &= ~WDG_CTRL_CLK_SRC_MASK;
	val |= sdrv_wdt->wdt_clk->src;

	clk_div = sdrv_wdt->clk_div;
	val |= WDG_CTRL_PRE_DIV_NUM(clk_div);

	/* disable auto restart */
	val &= ~WDG_CTRL_AUTO_RESTART_MASK;

	writel(val, wdt_ctl);

	/* wdt teminal count */
	timeout = sdrv_wdt->timeout;
	timeout *= 1000;
	wtc_con = timeout * (sdrv_wdt->wdt_clk->ration_to_ms/(clk_div + 1));

	writel(wtc_con, wdt_wtc);

	/* refresh mode select */
	if (sdrv_wdt->min_hw_heartbeat_ms) {
		/* wdt refresh window low limit */
		writel(sdrv_wdt->min_hw_heartbeat_ms * (sdrv_wdt->wdt_clk->ration_to_ms/(clk_div + 1)),
				wdt_wrc_val);
		/* setup time window refresh mode */
		writel(WDG_WRC_CTRL_MODEM0_MASK | WDG_WRC_CTRL_REFR_TRIG_MASK
				| WDG_WRC_CTRL_MODEM1_MASK, wdt_wrc_ctl);
	} else {
		/* setup direct refresh mode */
		writel(WDG_WRC_CTRL_MODEM0_MASK|WDG_WRC_CTRL_REFR_TRIG_MASK, wdt_wrc_ctl);
	}

	/* delay of overflow before reset */
	val = WDG_RST_CTRL_RST_CNT(sdrv_wdt->dying_delay);

	/* wdt rest ctl */
	if (sdrv_wdt->sig_rstgen) {
		val |= WDG_RST_CTRL_INT_RST_EN_MASK;
		/* level mode */
		val &= ~WDG_RST_CTRL_INT_RST_MODE_MASK;
	}
	writel(val, wdt_rst_ctl);

	/* enable wdt overflow int */
	writel(0x4, wdt_rst_int);

	/* enable wct */
	val = readl(wdt_ctl);
	writel(val|WDG_CTRL_WDG_EN_MASK, wdt_ctl);

	while(!(readl(wdt_ctl) & WDG_CTRL_WDG_EN_STA_MASK));

	val = readl(wdt_ctl);
	writel(val|WDG_CTRL_SOFT_RST_MASK, wdt_ctl);

	while(readl(wdt_ctl) & WDG_CTRL_SOFT_RST_MASK);

	return 0;
}

static int sdrv_wdt_is_running(struct semidrive_wdt_priv *sdrv_wdt)
{
	void __iomem *wdt_ctl = sdrv_wdt->base;
	return !!((readl(wdt_ctl) & WDG_CTRL_WDG_EN_STA_MASK)
			&& (readl(wdt_ctl + 0x4) != readl(wdt_ctl + 0x1c)));
}

#define BOOT_REASON_ADDRESS  0x38418000
#define REG_STATUS_ADDRESS   0x3841a000
void sdrv_set_bootreason(struct udevice *dev)
{
	int ret = 0, reason, val;

	reason = HALT_REASON_SW_GLOBAL_POR;

	val = readl(BOOT_REASON_ADDRESS);
	if (!ret) {
		val &= ~BOOT_REASON_MASK;
		val |= reason;
		writel(val,BOOT_REASON_ADDRESS);
	} else {
		pr_err("failed to save reboot reason\n");
	}

	writel(0,REG_STATUS_ADDRESS);

}

void sdrv_restart_without_reason(struct udevice *dev)
{
	struct semidrive_wdt_priv *sdrv_wdt = dev_get_priv(dev);

	if (!sdrv_wdt_is_running(sdrv_wdt)) {
		/* run watchdog, and don't kick it */
		flush_dcache_all();
		semidrive_wdt_start(dev,0,0);
	} else {
		/* kick watchdog with an impossible delay */
		sdrv_wdt->min_hw_heartbeat_ms =  0xffffffff;
	}
}

int sdrv_restart(struct udevice *dev, ulong flags)
{
	sdrv_set_bootreason(dev);
	sdrv_restart_without_reason(dev);

        return 0;
}

static int semidrive_wdt_probe(struct udevice *dev)
{
	struct semidrive_wdt_priv *sdrv_wdt = dev_get_priv(dev);
	const void *fdt = gd->fdt_blob;
	int node = dev_of_offset(dev);
	const fdt32_t *ret;;
	const char *str = NULL, *str1= NULL;
	int i, len;

	sdrv_wdt->base = dev_remap_addr(dev);
	if (!sdrv_wdt->base)
		return -EINVAL;

	str = fdt_stringlist_get(fdt, node, "wdt,clock-source", 0, NULL);
	if (str) {
		for (i = 0; i < sizeof(wdt_clk)/sizeof(wdt_clk[0]); i++) {
			if (!strcmp(wdt_clk[i].name, str))
				sdrv_wdt->wdt_clk = &wdt_clk[i];
		}
	} else {
		printf("wdt clock source not specified, select main clock\n");
		sdrv_wdt->wdt_clk = &wdt_clk[0];
	}

	sdrv_wdt->clk_div = fdtdec_get_uint(fdt, node, "wdt,clock-divider", 0);
	sdrv_wdt->min_hw_heartbeat_ms = fdtdec_get_uint(fdt, node, "wdt,min-hw-hearbeat", 0);
	sdrv_wdt->max_hw_heartbeat_ms = fdtdec_get_uint(fdt, node, "wdt,max-hw-hearbeat", 0);

	ret = fdt_getprop(fdt, node, "wdt,dying-delay", &len);
	if (ret)
		sdrv_wdt->dying_delay = SDRV_WDT_REBOOT_DELAY;

	sdrv_wdt->timeout = fdtdec_get_uint(fdt, node, "timeout-sec", 0);

	str1 = fdt_stringlist_get(fdt, node, "wdt,sig-rstgen", 0, NULL);
	if (str1 && !strcmp(str1, "true"))
		sdrv_wdt->sig_rstgen = 1;

	return 0;
}

static const struct wdt_ops semidrive_wdt_ops = {
	.expire_now = sdrv_restart,
};

static const struct udevice_id semidrive_wdt_ids[] = {
	{ .compatible = "semidrive,watchdog"},
	{}
};

U_BOOT_DRIVER(semidrive_wdt) = {
	.name = "semidrive_wdt",
	.id = UCLASS_WDT,
	.of_match = semidrive_wdt_ids,
	.priv_auto	= sizeof(struct semidrive_wdt_priv),
	.probe = semidrive_wdt_probe,
	.ops = &semidrive_wdt_ops,
	.flags = DM_FLAG_PRE_RELOC,
};
