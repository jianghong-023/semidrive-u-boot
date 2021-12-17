// SPDX-License-Identifier: GPL-2.0+
/*
 * Texas Instruments' OMAP serial driver
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <common.h>
#include <dm.h>
#include <dt-structs.h>
#include <log.h>
#include <serial.h>
#include <clk.h>
#include <linux/err.h>
#include <watchdog.h>

#ifndef CONFIG_SYS_NS16550_CLK
#define CONFIG_SYS_NS16550_CLK  0
#endif


#ifndef CONFIG_SYS_NS16550_IER
#define CONFIG_SYS_NS16550_IER  0x00
#endif

#define UART_LCR_WLS_8	0x03		/* 8 bit character length */
#define UART_LCR_DLAB	0x80		/* Divisor latch access bit */

#define UART_LSR_DR	0x01		/* Data ready */
#define UART_LSR_THRE	0x20		/* Xmit holding register empty */
#define UART_LSR_TEMT	0x40		/* Xmitter empty */

#define UART_MCR_DTR	0x01		/* DTR   */
#define UART_MCR_RTS	0x02		/* RTS   */

#define UART_FCR_FIFO_EN	0x01	/* Fifo enable */
#define UART_FCR_RXSR		0x02	/* Receiver soft reset */
#define UART_FCR_TXSR		0x04	/* Transmitter soft reset */
#define UART_MCRVAL 0x00
#define UART_LCRVAL UART_LCR_8N1

/* Clear & enable FIFOs */
#define UART_FCRVAL (UART_FCR_FIFO_EN | \
		     UART_FCR_RXSR |	\
		     UART_FCR_TXSR)

struct semidrive_serial_regs {
	union {
		u32 rbr;
		u32 dll;
		u32 thr;
	};
	union {
		u32 ier;
		u32 dlh;
	};
	union {
		u32 fcr;
		u32 iir;
	};
	u32 lcr;
	u32 mcr;
	u32 lsr;
	u32 msr;
	u32 scr;
	u32 reserve[4];
	u32 srbr[15];
	u32 far;
	u32 tfr;
	u32 rfw;
	u32 usr;
	u32 tfl;
	u32 rfl;
	u32 htx;
	u32 dmasa;
	u32 tcr;
	u32 de_en;
	u32 re_en;
	u32 det;
	u32 tat;
	u32 dlf;
	u32 rar;
	u32 tar;
	u32 lcr_ext;
	u32 cpr;
};

struct semidrive_priv {
	struct semidrive_serial_regs __iomem *regs;
	u32 clock;
};

static void _semidrive_serial_setbrg(struct semidrive_priv *priv, int baud)
{
	const unsigned int mode_x_div = 16;
	int quot;
	int frac;

	quot = DIV_ROUND_CLOSEST(CONFIG_DEBUG_UART_CLOCK,
					 mode_x_div * baud);

	frac = ((CONFIG_DEBUG_UART_CLOCK / ((baud * 16) / 1000)) -
		(quot * 1000));

	/* set divisor */
	writel(UART_LCR_WLS_8 | UART_LCR_DLAB, &priv->regs->lcr);
	writel(quot & 0xff, &priv->regs->dll);
	writel((quot >> 8) & 0xff, &priv->regs->dlh);
	writel(frac, &priv->regs->dlf);
	writel(UART_LCR_WLS_8, &priv->regs->lcr);
}

static int _semidrive_serial_putc(struct semidrive_priv *priv, const char ch)
{
	if (!(readl(&priv->regs->lsr) & UART_LSR_THRE))
		return -EAGAIN;

	writel(ch, &priv->regs->thr);

	if (ch == '\n')
		WATCHDOG_RESET();

	return 0;
}

static int _semidrive_serial_getc(struct semidrive_priv *priv)
{
	if (!(readl(&priv->regs->lsr) & UART_LSR_DR))
		return -EAGAIN;

	return readl(&priv->regs->rbr);
}

static int _semidrive_serial_pending(struct semidrive_priv *priv, bool input)
{
	if (input)
		return (readl(&priv->regs->lsr) & UART_LSR_DR) ? 1 : 0;
	else
		return (readl(&priv->regs->lsr) & UART_LSR_THRE) ? 0 : 1;
}

#if defined CONFIG_DM_SERIAL && \
	!defined CONFIG_SPL_BUILD || defined CONFIG_SPL_DM


static int semidrive_serial_setbrg(struct udevice *dev, int baudrate)
{
	struct semidrive_priv *priv = dev_get_priv(dev);

	_semidrive_serial_setbrg(priv, baudrate);

	return 0;
}

static int semidrive_serial_putc(struct udevice *dev, const char ch)
{
	struct semidrive_priv *priv = dev_get_priv(dev);

	return _semidrive_serial_putc(priv, ch);
}

static int semidrive_serial_getc(struct udevice *dev)
{
	struct semidrive_priv *priv = dev_get_priv(dev);

	return _semidrive_serial_getc(priv);
}

static int semidrive_serial_pending(struct udevice *dev, bool input)
{
	struct semidrive_priv *priv = dev_get_priv(dev);

	return _semidrive_serial_pending(priv, input);
}

static int semidrive_serial_probe(struct udevice *dev)
{
	struct semidrive_priv *priv = dev_get_priv(dev);

	/* Disable interrupt */
	writel(CONFIG_SYS_NS16550_IER, &priv->regs->ier);

	writel(UART_MCRVAL, &priv->regs->mcr);
	writel(UART_FCRVAL, &priv->regs->fcr);

	_semidrive_serial_setbrg(priv, CONFIG_BAUDRATE);
	return 0;
}

#if CONFIG_IS_ENABLED(OF_CONTROL) && !CONFIG_IS_ENABLED(OF_PLATDATA)
static int semidrive_serial_of_to_plat(struct udevice *dev)
{
	struct semidrive_priv *priv = dev_get_priv(dev);
	fdt_addr_t addr;
	int err;
	struct clk clk;

	addr = dev_read_addr(dev);
	if (addr == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->regs = map_physmem(addr, 0, MAP_NOCACHE);

	err = clk_get_by_index(dev, 0, &clk);
	if (!err) {
		err = clk_get_rate(&clk);
		if (!IS_ERR_VALUE(err))
			priv->clock = err;
	} else if (err != -ENOENT && err != -ENODEV && err != -ENOSYS) {
		debug("semidrive_serial: failed to get clock\n");
		return err;
	}

	if (!priv->clock)
		priv->clock = dev_read_u32_default(dev, "clock-frequency", 0);

	if (!priv->clock) {
		debug("semidrive_serial: clock not defined\n");
		return -EINVAL;
	}


	return 0;
}

static const struct dm_serial_ops semidrive_serial_ops = {
	.putc = semidrive_serial_putc,
	.pending = semidrive_serial_pending,
	.getc = semidrive_serial_getc,
	.setbrg = semidrive_serial_setbrg,
};

static const struct udevice_id semidrive_serial_ids[] = {
	{ .compatible = "snps,dw-apb-uart", },
	{}
};
#endif /* OF_CONTROL && !OF_PLATDATA */

U_BOOT_DRIVER(semidrive_serial) = {
	.name	= "semidrive_serial",
	.id	= UCLASS_SERIAL,
#if CONFIG_IS_ENABLED(OF_CONTROL) && !CONFIG_IS_ENABLED(OF_PLATDATA)
	.of_match = semidrive_serial_ids,
	.of_to_plat = semidrive_serial_of_to_plat,
#endif
	.priv_auto	= sizeof(struct semidrive_priv),
	.probe = semidrive_serial_probe,
	.ops	= &semidrive_serial_ops,
	.flags	= DM_FLAG_PRE_RELOC,
};

#else

DECLARE_GLOBAL_DATA_PTR;

#define DECLARE_UART_PRIV(port) \
	static struct semidrive_priv semidrive_uart##port = { \
	.regs = (struct semidrive_serial_regs *)CONFIG_SYS_NS16550_COM##port, \
	.clock = CONFIG_SYS_NS16550_CLK \
};

#define DECLARE_UART_FUNCTIONS(port) \
	static int semidrive_serial##port##_init(void) \
	{ \
		writel(0, &semidrive_uart##port.regs->ier); \
		writel(UART_MCRVAL, &semidrive_uart##port.regs->mcr); \
		writel(UART_FCRVAL, &semidrive_uart##port.regs->fcr); \
		_semidrive_serial_setbrg(&semidrive_uart##port, gd->baudrate); \
		return 0 ; \
	} \
	static void semidrive_serial##port##_setbrg(void) \
	{ \
		_semidrive_serial_setbrg(&semidrive_uart##port, gd->baudrate); \
	} \
	static int semidrive_serial##port##_getc(void) \
	{ \
		int err; \
		do { \
			err = _semidrive_serial_getc(&semidrive_uart##port); \
			if (err == -EAGAIN) \
				WATCHDOG_RESET(); \
		} while (err == -EAGAIN); \
		return err >= 0 ? err : 0; \
	} \
	static int semidrive_serial##port##_tstc(void) \
	{ \
		return _semidrive_serial_pending(&semidrive_uart##port, true); \
	} \
	static void semidrive_serial##port##_putc(const char c) \
	{ \
		int err; \
		if (c == '\n') \
			semidrive_serial##port##_putc('\r'); \
		do { \
			err = _semidrive_serial_putc(&semidrive_uart##port, c); \
		} while (err == -EAGAIN); \
	} \
	static void semidrive_serial##port##_puts(const char *s) \
	{ \
		while (*s) { \
			semidrive_serial##port##_putc(*s++); \
		} \
	}

/* Serial device descriptor */
#define INIT_UART_STRUCTURE(port, __name) {	\
	.name	= __name,			\
	.start	= semidrive_serial##port##_init,	\
	.stop	= NULL,				\
	.setbrg	= semidrive_serial##port##_setbrg,	\
	.getc	= semidrive_serial##port##_getc,	\
	.tstc	= semidrive_serial##port##_tstc,	\
	.putc	= semidrive_serial##port##_putc,	\
	.puts	= semidrive_serial##port##_puts,	\
}

#define DECLARE_UART(port, __name) \
	DECLARE_UART_PRIV(port); \
	DECLARE_UART_FUNCTIONS(port); \
	struct serial_device semidrive_uart##port##_device = \
		INIT_UART_STRUCTURE(port, __name);

#if !defined(CONFIG_CONS_INDEX)
#elif (CONFIG_CONS_INDEX < 1) || (CONFIG_CONS_INDEX > 6)
#error	"Invalid console index value."
#endif

#if CONFIG_CONS_INDEX == 1 && !defined(CONFIG_SYS_NS16550_COM1)
#error	"Console port 1 defined but not configured."
#elif CONFIG_CONS_INDEX == 2 && !defined(CONFIG_SYS_NS16550_COM2)
#error	"Console port 2 defined but not configured."
#elif CONFIG_CONS_INDEX == 3 && !defined(CONFIG_SYS_NS16550_COM3)
#error	"Console port 3 defined but not configured."
#elif CONFIG_CONS_INDEX == 4 && !defined(CONFIG_SYS_NS16550_COM4)
#error	"Console port 4 defined but not configured."
#elif CONFIG_CONS_INDEX == 5 && !defined(CONFIG_SYS_NS16550_COM5)
#error	"Console port 5 defined but not configured."
#elif CONFIG_CONS_INDEX == 6 && !defined(CONFIG_SYS_NS16550_COM6)
#error	"Console port 6 defined but not configured."
#endif

#if defined(CONFIG_SYS_NS16550_COM1)
DECLARE_UART(1, "semidrive_uart0");
#endif
#if defined(CONFIG_SYS_NS16550_COM2)
DECLARE_UART(2, "semidrive_uart1");
#endif
#if defined(CONFIG_SYS_NS16550_COM3)
DECLARE_UART(3, "semidrive_uart2");
#endif
#if defined(CONFIG_SYS_NS16550_COM4)
DECLARE_UART(4, "semidrive_uart3");
#endif
#if defined(CONFIG_SYS_NS16550_COM5)
DECLARE_UART(5, "semidrive_uart4");
#endif
#if defined(CONFIG_SYS_NS16550_COM6)
DECLARE_UART(6, "semidrive_uart5");
#endif

__weak struct serial_device *default_serial_console(void)
{
#if CONFIG_CONS_INDEX == 1
	return &semidrive_uart1_device;
#elif CONFIG_CONS_INDEX == 2
	return &semidrive_uart2_device;
#elif CONFIG_CONS_INDEX == 3
	return &semidrive_uart3_device;
#elif CONFIG_CONS_INDEX == 4
	return &semidrive_uart4_device;
#elif CONFIG_CONS_INDEX == 5
	return &semidrive_uart5_device;
#elif CONFIG_CONS_INDEX == 6
	return &semidrive_uart6_device;
#else
#error "Bad CONFIG_CONS_INDEX."
#endif
}

void semidrive_serial_initialize(void)
{
#if defined(CONFIG_SYS_NS16550_COM1)
	serial_register(&semidrive_uart1_device);
#endif
#if defined(CONFIG_SYS_NS16550_COM2)
	serial_register(&semidrive_uart2_device);
#endif
#if defined(CONFIG_SYS_NS16550_COM3)
	serial_register(&semidrive_uart3_device);
#endif
#if defined(CONFIG_SYS_NS16550_COM4)
	serial_register(&semidrive_uart4_device);
#endif
#if defined(CONFIG_SYS_NS16550_COM5)
	serial_register(&semidrive_uart5_device);
#endif
#if defined(CONFIG_SYS_NS16550_COM6)
	serial_register(&semidrive_uart6_device);
#endif
}

#endif

#ifdef CONFIG_DEBUG_UART_SEMIDRIVE

#include <debug_uart.h>

static inline void _debug_uart_init(void)
{
	struct semidrive_priv priv;

	priv.regs = (void *)CONFIG_DEBUG_UART_BASE;
	priv.clock = CONFIG_DEBUG_UART_CLOCK;

	writel(CONFIG_SYS_NS16550_IER, &priv.regs->ier);
	writel(UART_MCRVAL, &priv.regs->mcr);
	writel(UART_FCRVAL, &priv.regs->fcr);

	_semidrive_serial_setbrg(&priv, CONFIG_BAUDRATE);
}

static inline void _debug_uart_putc(int ch)
{
	struct semidrive_serial_regs __iomem *base = (void *)CONFIG_DEBUG_UART_BASE;

	while (!(readl(&base->lsr) & UART_LSR_THRE))
		;
	writel(ch, &base->thr);

	if (ch == '\n')
		WATCHDOG_RESET();
}

DEBUG_UART_FUNCS

#endif
