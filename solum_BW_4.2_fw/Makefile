CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy
LTO		= -flto
COMMON	= -Os -mthumb -march=armv7-m  -fomit-frame-pointer -I. $(LTO) -munaligned-access
CCFLAGS	= $(COMMON) -ffunction-sections -fdata-sections
LDFLAGS	= $(COMMON) -Wl,--gc-sections -Wl,-T $(LKR)
BUILD	?= BWR

APP			= PatchedFW
CHAR_IMGS	= chars.img


COMMONOBJS += crt.o main.o ccm.o timer.o chars.o settings.o util.o comms.o heap.o

all: QSPI.$(BUILD).bin


define one_build


QSPI.$1.bin:	$$(APP).$1.1.bin $(APP).$1.2.bin stock.$1.fw $$(CHAR_IMGS)
	cat stock.$1.fw $$(APP).$1.1.bin > $$@
	truncate -s 61440 $$@
	dd if=$(APP).${1}.2.bin of=$$@ bs=1 seek=$4 conv=notrunc 2>/dev/null
	cat $(CHAR_IMGS) >> $$@
	truncate -s 65536 $$@
	/bin/echo -ne "\\x00\\xf0\\x00\\x00" | dd bs=1 conv=notrunc seek=8 of=$$@ 2>/dev/null
	/bin/echo -ne $2 | dd bs=1 conv=notrunc seek=$3 of=$$@ 2>/dev/null

OBJS$1 = $$(COMMONOBJS) $5

$$(APP).$1.elf: CCFLAGS += -DTAG_$1 -DUI_MSG_COLR_BACK=$6 -DUI_MSG_COLR_LINE1=$7 -DUI_MSG_COLR_LINE2=$8 -DUI_MSG_COLR_LINE3=$9
$$(APP).$1.elf: LKR = mz100.$1.lkr
$$(APP).$1.elf: $$(OBJS$1) $$(LKR)
	$$(LD) -o $$@ $$(LDFLAGS) $$(OBJS$1)

endef

$(eval $(call one_build,BWR,"\\x5f\\xf8\\x00\\xf0\\x49\\xc6\\x10\\x00",27340,0,displayBWR.o printf.o,2,0,3,0))
$(eval $(call one_build,BW,"\\x00\\xbf\\x5f\\xf8\\x00\\xf0\\x99\\xd2\\x10\\x00",22706,12432,displayBW.o,0,3,2,3))

%.1.bin: %.elf
	$(OBJCOPY) -I elf32-littlearm -O binary $< $@ -j.text -j.data -j.rodata -j.vectors -j.ver

%.2.bin: %.elf
	$(OBJCOPY) -I elf32-littlearm -O binary $< $@ -j.text2

%.o : %.c Makefile
	$(CC) $(CCFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(APP).elf

