/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2021 Semidrive Electronics Co., Ltd.
 */

#ifndef __SEMIDRIVE_BOOT_H__
#define __SEMIDRIVE_BOOT_H__

#include <linux/types.h>

/* Boot device */
#define BOOT_DEVICE_RESERVED    0
#define BOOT_DEVICE_EMMC        1
#define BOOT_DEVICE_NAND        2
#define BOOT_DEVICE_SPI         3
#define BOOT_DEVICE_SD          4
#define BOOT_DEVICE_USB         5

#endif /* __SEMIDRIVE_BOOT_H__ */
