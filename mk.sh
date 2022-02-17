#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+

GCC_ARM_EABI="/tool/gcc_linaro/gcc-arm-none-eabi-7.3.1/bin"
GCC_X86_ELF="/tool/gcc_linaro/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-elf/bin"
GCC_X86_GNU="/tool/gcc_linaro/gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu/bin"

if [ ! -e $GCC_ARM_EABI -a ! -e $GCC_X86_ELF -a ! -e $GCC_X86_GNU ]; then
	echo -e "\n gcc tools is not found in the path"
	exit 1
fi

export PATH=$PATH:$GCC_ARM_EABI:$GCC_X86_ELF:$GCC_X86_GNU

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export AS=${CROSS_COMPILE}as
export LD=${CROSS_COMPILE}ld
export CC=${CROSS_COMPILE}gcc
export AR=${CROSS_COMPILE}ar
export NM=${CROSS_COMPILE}nm
export STRIP=${CROSS_COMPILE}strip
export OBJCOPY=${CROSS_COMPILE}objcopy
export OBJDUMP=${CROSS_COMPILE}objdump
export LOCALVERSION=""

make distclean
make d9_defconfig
#make d9lite_defconfig
#make d9plus_ap1_defconfig
#make d9plus_ap2_defconfig
make -j8
