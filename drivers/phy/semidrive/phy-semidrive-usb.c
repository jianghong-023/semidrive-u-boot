// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Semidrive Ltd
 * Written by Huanghuafeng  <huafeng.huang@semidrive.com>
 */

#include <common.h>
#include <dm.h>
#include <log.h>
#include <dm/device.h>
#include <generic-phy.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>

/* USB PHY control register offsets */
#define USB_PHY_NCR_CTRL0	0x10000

struct phy_semidrive_usb_priv {
	void __iomem *reg;
};

static int phy_semidrive_usb_init(struct phy *phy)
{
	unsigned int value;
	struct udevice *dev = phy->dev;
	struct phy_semidrive_usb_priv *priv = dev_get_priv(dev);

	/* use internal phy clock and reset usb phy, reset high effective */
	value = readl(priv->reg + USB_PHY_NCR_CTRL0);
	value &= ~(1 << 18);
	value |= (1 << 0);
	writel(value, priv->reg + USB_PHY_NCR_CTRL0);

	udelay(30);

	value = readl(priv->reg + USB_PHY_NCR_CTRL0);
	value &= ~(1 << 0);
	writel(value, priv->reg + USB_PHY_NCR_CTRL0);

	return 0;
}

static int semidrive_usb_phy_probe(struct udevice *dev)
{
	struct phy_semidrive_usb_priv *priv = dev_get_priv(dev);

	priv->reg = dev_remap_addr_index(dev, 0);
	if (!priv->reg) {
		pr_err("unable to remap usb phy\n");
		return -EINVAL;
	}
	return 0;
}

static const struct udevice_id semidrive_usb_phy_ids[] = {
	{ .compatible = "semidrive,usb-phy" },
	{ }
};

static struct phy_ops phy_semidrive_usb_ops = {
	.init = phy_semidrive_usb_init,
};

U_BOOT_DRIVER(semidrive_usb_phy) = {
	.name	= "semidrive_usb_phy",
	.id	= UCLASS_PHY,
	.of_match = semidrive_usb_phy_ids,
	.ops = &phy_semidrive_usb_ops,
	.probe = semidrive_usb_phy_probe,
	.priv_auto	= sizeof(struct phy_semidrive_usb_priv),
};
