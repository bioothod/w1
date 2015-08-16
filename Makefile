KDIR := /lib/modules/$(shell uname -r)/build
INCLUDES := -I${KDIR}/include
default: w1d.c
	gcc -W -Wall -O9 ${INCLUDES} w1d.c -o w1d
