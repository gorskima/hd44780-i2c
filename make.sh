#!/bin/bash

# A shortcut script for cross-compilation for Raspberry Pi

KERNELDIR=../linux
ARCH=arm
CROSS_COMPILE=../tools/arm-bcm2708/arm-bcm2708hardfp-linux-gnueabi/bin/arm-bcm2708hardfp-linux-gnueabi-
make $@

