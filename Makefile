obj-m+=framegrabber.o

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD)
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install
