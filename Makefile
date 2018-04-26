obj-m+=framegrabber.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
copy:
	scp framegrabber.c Makefile arp@arp:framegrabber/
	ssh arp@arp 'cd framegrabber && make && make install'
install:
	sudo cp framegrabber.ko /lib/modules/`uname -r`/kernel/drivers/pci/
