GZIP=gzip
# Windows' echo echos its input verbatim, on Posix there is some
#  amount of shell command line parsing going on. echo "" on 
#  Windows yields literal "", on Linux yields an empty line
ifeq ($(shell echo ""),)
# This is probably a proper system, so we can use uname
DELETE=rm -rf
FLASH_TOOL=client/flasher
platform=$(shell uname)
ifneq (,$(findstring MINGW,$(platform)))
FLASH_PORT=com3
PATHSEP=\\#
else
FLASH_PORT=/dev/ttyACM0
PATHSEP=/
endif
else
# Assume that we are running on native Windows
DELETE=del /q
FLASH_TOOL=client/flasher.exe
platform=Windows
FLASH_PORT=com3
PATHSEP=\\#
endif

all clean: %: client/% bootrom/% armsrc/% recovery/% mfkey/%

bootrom/%: FORCE
	$(MAKE) -C bootrom $(patsubst bootrom/%, %, $@)
armsrc/%: FORCE
	$(MAKE) -C armsrc $(patsubst armsrc/%, %, $@)
client/%: FORCE
	$(MAKE) -C client $(patsubst client/%, %, $@)
recovery/%: FORCE
	$(MAKE) -C recovery $(patsubst recovery/%, %, $@)
mfkey/%: FORCE
	$(MAKE) -C tools/mfkey $(patsubst mfkey/%, %, $@)
FORCE: # Dummy target to force remake in the subdirectories, even if files exist (this Makefile doesn't know about the prerequisites)

.PHONY: all clean help _test flash-bootrom flash-os flash-all FORCE

help:
	@echo Multi-OS Makefile, you are running on $(DETECTED_OS)
	@echo Possible targets:
	@echo +	all           - Make bootrom, armsrc and the OS-specific host directory
	@echo + client        - Make only the OS-specific host directory
	@echo + flash-bootrom - Make bootrom and flash it
	@echo + flash-os      - Make armsrc and flash os \(includes fpga\)
	@echo + flash-all     - Make bootrom and armsrc and flash bootrom and os image
	@echo +	clean         - Clean in bootrom, armsrc and the OS-specific host directory

client: client/all

mfkey: mfkey/all

flash-bootrom: bootrom/obj/bootrom.elf $(FLASH_TOOL)
	$(FLASH_TOOL) $(FLASH_PORT) -b $(subst /,$(PATHSEP),$<)

flash-os: armsrc/obj/fullimage.elf $(FLASH_TOOL)
	$(FLASH_TOOL) $(FLASH_PORT) $(subst /,$(PATHSEP),$<)

flash-all: bootrom/obj/bootrom.elf armsrc/obj/fullimage.elf $(FLASH_TOOL)
	$(FLASH_TOOL) $(FLASH_PORT) -b $(subst /,$(PATHSEP),$(filter-out $(FLASH_TOOL),$^))

newtarbin:
	$(DELETE) proxmark3-$(platform)-bin.tar proxmark3-$(platform)-bin.tar.gz
	@touch proxmark3-$(platform)-bin.tar

tarbin: newtarbin client/tarbin armsrc/tarbin bootrom/tarbin
	$(GZIP) proxmark3-$(platform)-bin.tar

# Dummy target to test for GNU make availability
_test:
