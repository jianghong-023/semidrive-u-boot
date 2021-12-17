/* SPDX-License-Identifier: GPL-2.0+
 *
 * Semidrive syscounter driver
 *
 * (C) Copyright 2021 huanghuafeng@semidrive.com
 */

#ifndef __INTERRUPT_H
#define __INTERRUPT_H

#include <irq_func.h>

#define IRQ_TYPE_NONE	0x0
#define IRQ_TYPE_EDGE_RISING	0x1
#define IRQ_TYPE_EDGE_FALLING	0x2
#define IRQ_TYPE_EDGE_BOTH	(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH	0x4
#define IRQ_TYPE_LEVEL_LOW	0x8
#define IRQ_TYPE_LEVEL_MASK	(IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH)

struct irq_action {
	interrupt_handler_t *handler;
	void *arg;
	int count;
};

struct irq_chip {
	void (*irq_enable)(unsigned int irq);
	void (*irq_disable)(unsigned int irq);
	void (*irq_eoi)(unsigned int irq);
	void (*irq_set_type)(unsigned int irq, unsigned int type);
};

struct irq_desc {
	struct irq_chip *chip;
	struct irq_action action;
};

void enable_irq(unsigned int irq);
void disable_irq(unsigned int irq);
int request_irq(unsigned int irq, interrupt_handler_t handler, void *arg, unsigned long type);
void free_irq(unsigned int irq);
void handle_irq_event(struct pt_regs *pt_regs, unsigned int esr);
int register_irqchip(const struct irq_chip *chip);

#endif
