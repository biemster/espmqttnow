TOOLCHAIN = /home/lars/temp/QCA9533/openwrt-toolchain-ath79-generic_gcc-8.4.0_musl.Linux-x86_64/toolchain-mips_24kc_gcc-8.4.0_musl
CC = $(TOOLCHAIN)/bin/mips-openwrt-linux-musl-gcc
INC = $(TOOLCHAIN)/include/
LIB = $(TOOLCHAIN)/lib/

all:
	mkdir -p bin
	$(CC) -s -march=24kc -ffreestanding espmqttnow.c mqtt.c mqtt_pal.c -I include -Wall -Os -o bin/espmqttnow

clean:
	rm -r bin
