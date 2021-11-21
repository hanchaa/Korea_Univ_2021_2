# @author: 차주한, 김경하
# 2021.11.20
# netfilter make file

obj-m += netfilter.o

KDIR = /usr/src/linux-4.4

all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -rf *.o *.ko *.mod.* *.symbers *.order