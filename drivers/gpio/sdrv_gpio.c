// SPDX-License-Identifier: GPL-2.0+
/*
 * Semidrive GPIO driver
 *
 * (C) Copyright 2021 shide.zhou@semidrive.com
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <asm/global_data.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <dm/devres.h>
#include <dm/device_compat.h>
#include <dm/device-internal.h>

DECLARE_GLOBAL_DATA_PTR;

#define GPIO_DATA_IN_PORT_1		    0x2200
#define GPIO_DATA_IN_PORT_2		    0x2210
#define GPIO_DATA_IN_PORT_SIZE \
	(GPIO_DATA_IN_PORT_2 - GPIO_DATA_IN_PORT_1)
#define GPIO_DATA_IN_PORT_X(n) \
	(GPIO_DATA_IN_PORT_1 + ((n) * GPIO_DATA_IN_PORT_SIZE))

#define GPIO_DATA_OUT_PORT_1		0x2400
#define GPIO_DATA_OUT_PORT_2		0x2410
#define GPIO_DATA_OUT_PORT_SIZE \
	(GPIO_DATA_OUT_PORT_2 - GPIO_DATA_OUT_PORT_1)
#define GPIO_DATA_OUT_PORT_X(n) \
	(GPIO_DATA_OUT_PORT_1 + ((n) * GPIO_DATA_OUT_PORT_SIZE))

#define GPIO_DIR_PORT_1		        0x2000
#define GPIO_DIR_PORT_2		        0x2010
#define GPIO_DIR_PORT_SIZE \
	(GPIO_DIR_PORT_2 - GPIO_DIR_PORT_1)
#define GPIO_DIR_PORT_X(n) \
	(GPIO_DIR_PORT_1 + ((n) * GPIO_DIR_PORT_SIZE))

#define GPIO_IN_VALUE_MASK   1
#define GPIO_FUNCTION_MASK   1

struct sdrv_port {
	phys_addr_t base;
	unsigned int	idx;
	unsigned int	ngpio;
	unsigned int	gpio_ranges[4];
	char name[32];
};

struct sdrv_gpio_bank {
	phys_addr_t base;
	int nports;
};

static int sdrv_gpio_direction_input(struct udevice *dev, unsigned int gpio)
{
	struct sdrv_port *port_priv = dev_get_plat(dev);
	int  grp_idx = port_priv->idx;
	int  bit_num = gpio % (port_priv->ngpio);
	phys_addr_t reg = port_priv->base + GPIO_DIR_PORT_X(grp_idx);

	debug("dir_reg is %llx,bit num is %d\n",reg, bit_num);

	/* 0 is input */
	clrbits_le32(reg, 1<<bit_num);

	return 0;
}

static int sdrv_gpio_direction_output(struct udevice *dev, unsigned gpio,
				     int value)
{
	struct sdrv_port *port_priv = dev_get_plat(dev);
	int  grp_idx = port_priv->idx;
	int  bit_num = gpio % (port_priv->ngpio);
	phys_addr_t dir_reg = port_priv->base + GPIO_DIR_PORT_X(grp_idx);
	phys_addr_t out_reg = port_priv->base + GPIO_DATA_OUT_PORT_X(grp_idx);

	debug("dir_reg is %llx,out_reg is %llx bit num is %d\n",dir_reg, out_reg, bit_num);

	/* 1 is output */
	setbits_le32(dir_reg, 1<<bit_num);

	if (value)
		setbits_le32(out_reg, 1<<bit_num);
	else
		clrbits_le32(out_reg, 1<<bit_num);

	return 0;
}

static int sdrv_gpio_set_value(struct udevice *dev, unsigned gpio, int value)
{
	struct sdrv_port *port_priv = dev_get_plat(dev);
	int  grp_idx = port_priv->idx;
	int  bit_num = gpio % (port_priv->ngpio);
	phys_addr_t reg = port_priv->base + GPIO_DATA_OUT_PORT_X(grp_idx);

	if (value)
	  	setbits_le32(reg, 1<<bit_num);
	else
	   	clrbits_le32(reg, 1<<bit_num);

	return 0;
}

static int sdrv_gpio_get_function(struct udevice *dev, unsigned offset)
{
	struct sdrv_port *port_priv = dev_get_plat(dev);
	int  grp_idx = port_priv->idx;
	int  bit_num = offset % (port_priv->ngpio);
	phys_addr_t reg = port_priv->base + GPIO_DIR_PORT_X(grp_idx);

	if (readl(reg) >> bit_num & GPIO_FUNCTION_MASK)
		return GPIOF_OUTPUT;

	return GPIOF_INPUT;
}

static int sdrv_gpio_get_value(struct udevice *dev, unsigned gpio)
{
	struct sdrv_port *port_priv = dev_get_plat(dev);
	int  grp_idx = port_priv->idx;
	int  bit_num = gpio % (port_priv->ngpio);
	phys_addr_t in_reg = port_priv->base + GPIO_DATA_IN_PORT_X(grp_idx);
	phys_addr_t out_reg = port_priv->base + GPIO_DATA_OUT_PORT_X(grp_idx);

	debug("in_reg is %llx,bit num is %d\n",in_reg, bit_num);

	if (sdrv_gpio_get_function(dev, gpio) == GPIOF_OUTPUT)
	    return (readl(out_reg) >> bit_num) & GPIO_IN_VALUE_MASK;

	return (readl(in_reg) >> bit_num) & GPIO_IN_VALUE_MASK;
}


static const struct dm_gpio_ops gpio_sdrv_ops = {
	.direction_input	= sdrv_gpio_direction_input,
	.direction_output	= sdrv_gpio_direction_output,
	.get_value		= sdrv_gpio_get_value,
	.set_value		= sdrv_gpio_set_value,
	.get_function		= sdrv_gpio_get_function,
};

static int sdrv_gpio_probe(struct udevice *dev)
{
	return 0;
}

static int sdrv_gpio_of_to_plat(struct udevice *dev)
{
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct sdrv_gpio_bank *gpio_bank = dev_get_priv(dev);
	struct sdrv_port *port_priv;
	fdt_addr_t addr;
	struct udevice *subdev;
	int ret;
	ofnode subnode;

	addr = dev_read_addr(dev);
	gpio_bank->base = (unsigned long)map_physmem(addr, 0, MAP_NOCACHE);

	if (strstr(dev->name,"30420000"))
	    uc_priv->bank_name = "gpio4";
	else if (strstr(dev->name,"30430000"))
	    uc_priv->bank_name = "gpio5";
	else {
	    port_priv = dev_get_plat(dev);
	    uc_priv->bank_name = port_priv->name;
	    uc_priv->gpio_base = port_priv->gpio_ranges[2];
	    uc_priv->gpio_count = port_priv->ngpio;
	}

	for (subnode = dev_read_first_subnode(dev); ofnode_valid(subnode);
	     subnode = dev_read_next_subnode(subnode)) {
                struct sdrv_port *port_priv;
		if (!ofnode_read_bool(subnode, "gpio-controller"))
			continue;

		port_priv = devm_kzalloc(dev, sizeof(struct sdrv_port),GFP_KERNEL);
		if (!port_priv) {
			dev_err(dev, "Cannot alloc mem for port\n");
			return -ENOMEM;
		}

		port_priv->idx = ofnode_read_u32_default(subnode, "reg", 0);
 		port_priv->ngpio = ofnode_read_u32_default(subnode, "nr-gpios", 0);
		ret = ofnode_read_u32_array(subnode, "gpio-ranges", port_priv->gpio_ranges,
		        ARRAY_SIZE(port_priv->gpio_ranges));
		if (ret < 0) {
		    dev_err(dev, "Cannot read gpio-ranges property\n");
		    return -EINVAL;
		}
		port_priv->base = gpio_bank->base;

		snprintf(port_priv->name, sizeof(port_priv->name), "%s%c",
		         uc_priv->bank_name,port_priv->idx + 'a');

		ret = device_bind(dev, dev->driver, port_priv->name, port_priv, subnode,
				  &subdev);
		if (ret)
			return ret;
	}

	dev_dbg(dev, "bank_name is %s,ngpios is %d\n",uc_priv->bank_name,uc_priv->gpio_count);

	return 0;
}

static const struct udevice_id sdrv_gpio_ids[] = {
	{ .compatible = "semidrive,sdrv-gpio" },
	{ }
};

U_BOOT_DRIVER(gpio_sdrv) = {
	.name	= "gpio-sdrv",
	.id	= UCLASS_GPIO,
	.of_match = sdrv_gpio_ids,
	.of_to_plat = sdrv_gpio_of_to_plat,
	.probe	= sdrv_gpio_probe,
	.ops	= &gpio_sdrv_ops,
	.priv_auto	= sizeof(struct sdrv_gpio_bank),
};
