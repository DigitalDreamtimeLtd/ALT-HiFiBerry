VERBOSE ?= 0
ARCH ?= arm
CROSS_COMPILE ?=
KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
BUILD_DIR = $(shell pwd)

##
## GENERAL
##
# DEBUG
# DDEBUG

##
## DAC2HD
##
# CLK_DAC2HD_PREPARE_INIT
# CLK_DAC2HD_STATIC_DEFAULTS
# DAC2HD_DRVDATA
# PCM1796_GPIO_ACTIVE_HIGH
# PCM1796_MUTE_SWITCH

##
## DACPLUS
##
# PCM512X_GPIO_ACTIVE_HIGH

MY_CFLAGS ?= -DDEBUG -DDDEBUG -DPCM1796_GPIO_MUTE -DPCM1796_OUTPUT_ENABLE\
 -DCLK_DAC2HD_PREPARE_INIT -DCLK_DAC2HD_STATIC_DEFAULTS
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}

snd-soc-zpcm512x-i2c-objs := zpcm512x-i2c.o
snd-soc-zpcm512x-objs := zpcm512x.o dd-utils.o
snd-soc-zhifiberry-dacplus-objs := zhifiberry_dacplus.o

snd-soc-pcm1796-i2c-objs := pcm1796-i2c.o
snd-soc-pcm1796-objs := pcm1796.o dd-utils.o
snd-soc-hifiberry-dac2hd-objs := hifiberry_dac2hd.o

obj-m := \
 clk-hifiberry-dacpluspro.o\
 snd-soc-zhifiberry-dacplus.o\
 snd-soc-zpcm512x-i2c.o\
 snd-soc-zpcm512x.o\
 clk-hifiberry-dac2hd.o\
 snd-soc-hifiberry-dac2hd.o\
 snd-soc-pcm1796-i2c.o\
 snd-soc-pcm1796.o

all: modules dtbs

modules:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC)\
	 M=$(BUILD_DIR) KBUILD_VERBOSE=$(VERBOSE) modules

dtbs:
	$(KSRC)/scripts/dtc/dtc -@ -I dts -O dtb\
	 -o hb-dacplus-audio.dtbo hb-dacplus-audio-overlay.dts

	$(KSRC)/scripts/dtc/dtc -@ -I dts -O dtb\
	 -o hb-dacplus-gmute-audio.dtbo hb-dacplus-gmute-audio-overlay.dts

	$(KSRC)/scripts/dtc/dtc -@ -I dts -O dtb\
	 -o hb-dac2hd-audio.dtbo hb-dac2hd-audio-overlay.dts

	$(KSRC)/scripts/dtc/dtc -@ -I dts -O dtb\
	 -o hb-dac2hd-gmute-audio.dtbo hb-dac2hd-gmute-audio-overlay.dts

clean:
	make -C $(KSRC) M=$(BUILD_DIR) clean
	rm -f *.dtbo

modules_install: modules
	install -d /lib/modules/$(KVER)/extra
	install -p -m 0644 *.ko /lib/modules/$(KVER)/extra
	depmod -a $(KVER)

dtbs_install: dtbs
	install -p -m 0755 *.dtbo /boot/overlays
	install -p -m 0755 README.* /boot/overlays

install: modules_install dtbs_install
