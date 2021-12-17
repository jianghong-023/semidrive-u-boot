// SPDX-License-Identifier: GPL-2.0+
/*
 * irq manage driver
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#define pr_fmt(fmt) "irq manage: " fmt

#include <interrupts.h>
#include <linux/errno.h>
#include <log.h>
#include <asm/io.h>
#include <asm/gic.h>
#include <config.h>
#include <common.h>
#include <dm.h>
#include <dt-structs.h>
#include <dm/device-internal.h>

static struct irq_desc g_irq_desc[CONFIG_NR_IRQS];

void enable_irq(unsigned int irq)
{
	struct irq_desc *irq_desc = &g_irq_desc[irq];
	struct irq_chip *irq_chip = NULL;

	if (!irq_desc) {
		log_err("Not support this irq:%u\n", irq);
		return;
	}

	irq_chip = irq_desc->chip;
	if (!irq_chip) {
		log_err("Don't have irq chip support!\n");
		return;
	}

	irq_chip->irq_enable(irq);
}

void disable_irq(unsigned int irq)
{
	struct irq_desc *irq_desc = &g_irq_desc[irq];
	struct irq_chip *irq_chip = NULL;

	if (!irq_desc) {
		log_err("Not support this irq:%u\n", irq);
		return;
	}

	irq_chip = irq_desc->chip;
	if (!irq_chip) {
		log_err("Don't have irq chip support!\n");
		return;
	}

	irq_chip->irq_disable(irq);
}

int request_irq(unsigned int irq, interrupt_handler_t handler, void *arg, unsigned long type)
{
	struct irq_desc *irq_desc = &g_irq_desc[irq];
	struct irq_chip *irq_chip = NULL;

	if (!irq_desc) {
		log_err("Not support this irq:%u\n", irq);
		return -EINVAL;
	}

	irq_chip = irq_desc->chip;
	if (!irq_chip) {
		log_err("Don't have irq chip support!\n");
		return -EINVAL;
	}

	if (!handler) {
		log_err("the handler arg can't be NULL!\n");
		return -EINVAL;
	}
	irq_desc->action.handler = handler;
	irq_desc->action.arg = arg;

	irq_desc->chip->irq_set_type(irq, type);
	return 0;
}

void free_irq(unsigned int irq)
{
	struct irq_desc *irq_desc = &g_irq_desc[irq];

	/* Free irq action */
	irq_desc->action.handler = NULL;
	irq_desc->action.arg = NULL;
}

void handler_bad_action(void *arg)
{
	log_err("Bad mode in \"Irq\" handler\n");
}

static int gic_get_irqinfo(void)
{
	unsigned int irqstat, irqnr;

	irqstat = readl(GICC_BASE + GICC_IAR);
	irqnr = irqstat & GICC_IAR_INT_ID_MASK;
	return irqnr;
}

void handle_irq_event(struct pt_regs *pt_regs, unsigned int esr)
{
	struct irq_desc *irq_desc = NULL;
	struct irq_action *action = NULL;
	struct irq_chip *irq_chip = NULL;
	unsigned int irqnr;

	do {
		irqnr = gic_get_irqinfo();
		irq_desc = &g_irq_desc[irqnr];
		if (!irq_desc) {
			log_err("Not support this irq:%u\n", irqnr);
			break;
		}

		if (irqnr > 15 && irqnr < 1020) {
			irq_chip = irq_desc->chip;
			if (!irq_chip) {
				log_err("Don't have irq chip support!\n");
				break;
			}

			action = &irq_desc->action;
			if (!action) {
				log_err("Need request irq first!\n");
				break;
			}
			if (!action->handler) {
				action->handler = handler_bad_action;
				action->handler(action->arg);
			}
			action->handler(action->arg);

			if (irq_chip->irq_eoi)
				irq_chip->irq_eoi(irqnr);
			continue;
		} else {
			/* IPI interrupt */
			log_debug("Not support!\n");
			break;
		}
	} while (1);
}

int register_irqchip(const struct irq_chip *chip)
{
	int i;

	for (i = 0; i < CONFIG_NR_IRQS; i++)
		g_irq_desc[i].chip = chip;

	return 0;
}

static int irqchip_post_bind(struct udevice *dev)
{
	struct udevice *devices;
	int ret;

	/* Be careful, the index = 0 */
	ret = uclass_get_device_by_seq(UCLASS_IRQ, 0, &devices);
	if (ret < 0) {
		printf("probe error!\n");
		return ret;
	}
	return 0;
}

UCLASS_DRIVER(irq) = {
	.id		= UCLASS_IRQ,
	.name		= "irq",
	.post_bind	= irqchip_post_bind,
};
