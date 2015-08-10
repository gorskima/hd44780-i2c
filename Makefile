ifneq ($(KERNELRELEASE),)
	obj-m := hd44780.o
	hd44780-y := hd44780-i2c.o hd44780-dev.o

else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
endif

