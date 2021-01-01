ifneq ($(KERNELRELEASE),)

obj-m:=rbtree.o

else

KDIR:=~/workspace/linux/build

default: rbtree.ko
rbtree.ko: rbtree.c
	make -C $(KDIR) M=$$PWD modules

install: rbtree.ko
	cp $^ ~/qemu-lab/rootfs/modules/
	~/qemu-lab/scripts/mkrootfs.sh

clean:
	make -C $(KDIR) M=$$PWD clean
help:
	make -C $(KDIR) M=$$PWD help

endif
