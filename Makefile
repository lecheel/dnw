KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build
#KERNEL_DIR      := /work/CNXTv20/kernel/output
#KERNEL_DIR      := /work/aspire/linux
#CROSS_COMPILE   := arm-linux-


PWD		:= $(shell pwd)

obj-m		:= dnw.o
dnw-objs   	:= dnw_usb.o

all: dnw

dnw:
	@echo "Building DNW OTG USB driver..."
	@(make -C $(KERNEL_DIR) M=$(PWD) CROSS_COMPILE=$(CROSS_COMPILE) modules)

clean:
	-rm -f *.o *.ko .*.cmd .*.flags *.mod.c Module.symvers modules.order tags
	-rm -rf .tmp_versions

