obj-m += stratopi.o

stratopi-objs := module.o soft_uart/raspberry_soft_uart.o soft_uart/queue.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean

install:
	sudo install -m 644 -c stratopi.ko /lib/modules/$(shell uname -r)
	sudo depmod
