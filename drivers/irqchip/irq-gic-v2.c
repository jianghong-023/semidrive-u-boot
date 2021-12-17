// SPDX-License-Identifier: GPL-2.0
/*
 * gic driver for uboot
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#define pr_fmt(fmt) "gic: " fmt

#include <common.h>
#include <dm.h>
#include <interrupts.h>
#include <linux/errno.h>
#include <log.h>
#include <asm/gic.h>
#include <asm/io.h>

struct gic_chip_data {
	void __iomem *dist_base;
	void __iomem *cpu_base;
	int gic_irqs;
};

static void *get_driver_priv(void)
{
	int ret;
	struct udevice *dev;
	struct gic_chip_data *priv;

	ret = uclass_get_device_by_seq(UCLASS_IRQ, 0, &dev);
	if (ret < 0) {
		log_err("Failed to find gic chip devices. Check device tree");
		return NULL;
	}

	priv = dev_get_priv(dev);

	return priv;
}

static void gic_enable_irq(unsigned int irq)
{
	struct gic_chip_data *priv;
	unsigned int mask;

	priv = (struct gic_chip_data *)get_driver_priv();
	if (!priv || !priv->dist_base) {
		log_err("enable irq error\n");
		return;
	}
	mask = readl(priv->dist_base + GICD_ISENABLERn + (irq / 32) * 4);
	mask |= 1 << (irq % 32);
	writel(mask, priv->dist_base + GICD_ISENABLERn + (irq / 32) * 4);
}

static void gic_disable_irq(unsigned int irq)
{
	struct gic_chip_data *priv;
	unsigned int mask;

	priv = (struct gic_chip_data *)get_driver_priv();
	if (!priv || !priv->dist_base) {
		log_err("disable irq error\n");
		return;
	}

	mask = readl(priv->dist_base + GICD_ICENABLERn + (irq / 32) * 4);
	mask |= 1 << (irq % 32);
	writel(mask, priv->dist_base +
			GICD_ICENABLERn + (irq / 32) * 4);
}

static void gic_eoi_irq(unsigned int irq)
{
	struct gic_chip_data *priv;

	priv = (struct gic_chip_data *)get_driver_priv();
	if (!priv || !priv->cpu_base) {
		log_err("gic eoi error\n");
		return;
	}
	writel(irq, priv->cpu_base +  GICC_EOIR);
}

static void gic_set_type(unsigned int irq, unsigned int type)
{
	unsigned int confmask = 0x2 << ((irq % 16) * 2);
	unsigned int confoff = (irq / 16) * 4;
	unsigned int val, oldval;
	struct gic_chip_data *priv;

	if (irq < 16)
		return;

	if (irq > 32 && type != IRQ_TYPE_LEVEL_HIGH &&
		type != IRQ_TYPE_EDGE_RISING)
		return;

	priv = (struct gic_chip_data *)get_driver_priv();
	if (!priv || !priv->dist_base) {
		log_err("driver init error, Please check!\n");
		return;
	}

	oldval = readl(priv->dist_base + GICD_ICFGR + confoff);
	val = oldval;
	if (type & IRQ_TYPE_LEVEL_MASK)
		val &= ~confmask;
	else if (type & IRQ_TYPE_EDGE_BOTH)
		val |= confmask;

	/* No need to change */
	if (val == oldval)
		return;

	/* change the config */
	writel(val, priv->dist_base + GICD_ICFGR + confoff);
	if (readl(priv->dist_base + GICD_ICFGR + confoff) != val) {
		log_err("gic set type error");
		return;
	}
}

static const struct irq_chip gic_irq_ops = {
	.irq_enable		= gic_enable_irq,
	.irq_disable		= gic_disable_irq,
	.irq_eoi		= gic_eoi_irq,
	.irq_set_type		= gic_set_type,
};

static void gic_dist_init(struct gic_chip_data *priv)
{
	unsigned int gic_irqs = priv->gic_irqs;
	void __iomem *base = priv->dist_base;
	unsigned int i;

	writel(GICD_DISABLE, base + GICD_CTLR);

	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for (i = 32; i < gic_irqs; i += 16)
		writel(GICD_INT_ACTLOW_LVLTRIG, base + GICD_ICFGR + i / 4);

	/*
	 * Deactivate and disable all SPIs.
	 * Leave the PPI and SGIs alone as they are in the redistributor
	 * registers on GICv3.
	 */
	for (i = 32; i < gic_irqs; i += 32) {
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       base + GICD_ICACTIVERn + i / 8);
		writel_relaxed(GICD_INT_EN_CLR_X32,
			       base + GICD_ICENABLERn + i / 8);
	}

	/*
	 * Deal with the banked PPI interrupts, disable all PPI interrupts
	 */
	writel(GICD_INT_EN_CLR_X32, base + GICD_ICACTIVERn);
	writel(GICD_INT_EN_CLR_PPI, base + GICD_ICENABLERn);

	/*
	 * Enable group0 interrupts
	 */
	writel(GICD_ENABLE, base + GICD_CTLR);
}

static void gic_cpu_init(struct gic_chip_data *priv)
{
	void __iomem *base = priv->cpu_base;

	writel(GICC_ENABLE, base + GICC_CTLR);
}

static int gicv2_probe(struct udevice *dev)
{
	struct gic_chip_data *priv = dev_get_priv(dev);
	unsigned int val;
	int ret;
	int gic_irqs;

	priv->dist_base = (void __iomem *)dev_read_addr_index(dev, 0);
	if (!priv->dist_base) {
		log_err("No device support\n");
		return -ENOENT;
	}

	priv->cpu_base = (void __iomem *)dev_read_addr_index(dev, 1);
	if (!priv->cpu_base) {
		log_err("No device support\n");
		return -ENOENT;
	}

	gic_irqs = readl(priv->dist_base + GICD_TYPER) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;

	priv->gic_irqs = gic_irqs;

	gic_dist_init(priv);

	gic_cpu_init(priv);
	ret = register_irqchip(&gic_irq_ops);
	if (ret < 0) {
		log_err("register irqchip error!\n");
		return ret;
	}

	log_info("gic probe finish!\n");
	return 0;
}

static const struct udevice_id gic_irq_ids[] = {
	{ .compatible = "arm,gic-400"},
	{ }
};

U_BOOT_DRIVER(gic_irq_drv) = {
	.name		= "gic_irq",
	.id		= UCLASS_IRQ,
	.of_match	= gic_irq_ids,
	.priv_auto	= sizeof(struct gic_chip_data),
	.probe		= gicv2_probe,
	.ops		= &gic_irq_ops,
	.flags		= DM_FLAG_ALLOC_PDATA,
};
