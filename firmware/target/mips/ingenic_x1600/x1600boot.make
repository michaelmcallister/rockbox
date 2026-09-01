#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
#
# X1600 bootloader build rules.
#
# Modelled on ingenic_x1000/x1000boot.make. The differences are:
#
#  * $(SPLBINARY) is produced by tools/mkspl-x1600, not mkspl-x1000. The X1600
#    BootROM checks two extra CRC-7s (env and header) that mkspl-x1000 leaves
#    as zero, and its length field is 16-bit not 32-bit. See tools/mkspl-x1600.c.
#
#  * There is an extra, DELIBERATELY NON-DEFAULT target, `usbstage1`. It links
#    the same objects against usbstage1.lds to produce a payload for BootROM USB
#    recovery, which must live above 0x80009000 while the flash SPL lives at
#    0x80001800. These two are NOT the same binary on the X1600 -- read
#    firmware/export/x1600.h. It is not part of $(BINARY) because nothing
#    consumes it automatically yet; build it explicitly with `make usbstage1`.

include $(ROOTDIR)/lib/microtar/microtar.make

INCLUDES += -I$(APPSDIR)
SRC += $(call preprocess, $(APPSDIR)/SOURCES)

LDSDEP := $(FIRMDIR)/export/x1600.h $(FIRMDIR)/export/config.h

BOOTLDS := $(FIRMDIR)/target/$(CPU)/$(MANUFACTURER)/boot.lds
BOOTLINK := $(BUILDDIR)/boot.link
BOOTEXT := $(suffix $(BINARY))

SPLLDS := $(FIRMDIR)/target/$(CPU)/$(MANUFACTURER)/spl.lds
SPLLINK := $(BUILDDIR)/spl.link
SPLBINARY := spl$(BOOTEXT)

STAGE1LDS := $(FIRMDIR)/target/$(CPU)/$(MANUFACTURER)/usbstage1.lds
STAGE1LINK := $(BUILDDIR)/usbstage1.link

BLINFO = $(BUILDDIR)/bootloader-info.txt

CLEANOBJS += $(BUILDDIR)/bootloader.* $(BUILDDIR)/spl.* $(BUILDDIR)/usbstage1.*

.SECONDEXPANSION:

### Bootloader

$(BOOTLINK): $(BOOTLDS) $(LDSDEP)
	$(call PRINTS,PP $(@F))
	$(call preprocess2file,$<,$@,)

$(BUILDDIR)/bootloader.elf: $$(OBJ) $(FIRMLIB) $(CORE_LIBS) $$(BOOTLINK)
	$(call PRINTS,LD $(@F))$(CC) $(GCCOPTS) -Os -nostdlib -o $@ $(OBJ) \
		-L$(BUILDDIR)/firmware -lfirmware \
		-L$(BUILDDIR)/lib $(call a2lnk, $(CORE_LIBS)) \
		-lgcc -T$(BOOTLINK) $(GLOBAL_LDOPTS) \
		-Wl,--gc-sections -Wl,-Map,$(BUILDDIR)/bootloader.map

$(BUILDDIR)/bootloader.bin: $(BUILDDIR)/bootloader.elf
	$(call PRINTS,OC $(@F))$(call objcopy,$<,$@)

$(BUILDDIR)/bootloader.ucl: $(BUILDDIR)/bootloader.bin
	$(call PRINTS,UCLPACK $(@F))$(TOOLSDIR)/uclpack --nrv2e -9 $< $@ >/dev/null


### SPL (flash boot, linked at 0x80001800)

$(SPLLINK): $(SPLLDS) $(LDSDEP)
	$(call PRINTS,PP $(@F))
	$(call preprocess2file,$<,$@,)

$(BUILDDIR)/spl.elf: $$(OBJ) $(FIRMLIB) $(CORE_LIBS) $$(SPLLINK)
	$(call PRINTS,LD $(@F))$(CC) $(GCCOPTS) -Os -nostdlib -o $@ $(OBJ) \
		-L$(BUILDDIR)/firmware -lfirmware \
		-L$(BUILDDIR)/lib $(call a2lnk, $(CORE_LIBS)) \
		-lgcc -T$(SPLLINK) $(GLOBAL_LDOPTS) \
		-Wl,--gc-sections -Wl,-Map,$(BUILDDIR)/spl.map

$(BUILDDIR)/spl.bin: $(BUILDDIR)/spl.elf
	$(call PRINTS,OC $(@F))$(call objcopy,$<,$@)

$(BUILDDIR)/$(SPLBINARY): $(BUILDDIR)/spl.bin
	$(call PRINTS,MKSPL $(@F))$(MKFIRMWARE) $< $@


### USB stage1 (BootROM recovery, linked at 0x8000a000) -- explicit target only

$(STAGE1LINK): $(STAGE1LDS) $(LDSDEP)
	$(call PRINTS,PP $(@F))
	$(call preprocess2file,$<,$@,)

$(BUILDDIR)/usbstage1.elf: $$(OBJ) $(FIRMLIB) $(CORE_LIBS) $$(STAGE1LINK)
	$(call PRINTS,LD $(@F))$(CC) $(GCCOPTS) -Os -nostdlib -o $@ $(OBJ) \
		-L$(BUILDDIR)/firmware -lfirmware \
		-L$(BUILDDIR)/lib $(call a2lnk, $(CORE_LIBS)) \
		-lgcc -T$(STAGE1LINK) $(GLOBAL_LDOPTS) \
		-Wl,--gc-sections -Wl,-Map,$(BUILDDIR)/usbstage1.map

# A plain objcopy -O binary would try to pad the file from offset 0x8000a000.
$(BUILDDIR)/usbstage1.bin: $(BUILDDIR)/usbstage1.elf
	$(call PRINTS,OC $(@F))$(OC) -O binary -j .text \
		--change-section-lma .text=0 $< $@

.PHONY: usbstage1
usbstage1: $(BUILDDIR)/usbstage1.bin


### Generating the update package

# suppress regenerating bootloader-info if nothing has changed
BLVERSION:=$(SVNVERSION)
OLDBLVERSION:=$(shell head -n1 $(BLINFO) 2>/dev/null || echo "NOREVISION")

ifneq ($(BLVERSION),$(OLDBLVERSION))
.PHONY: $(BLINFO)
endif

$(BLINFO):
	$(call PRINTS,GEN $(@F))echo $(SVNVERSION) > $@

# The "binary" is actually an update package which is just a tar archive.
#
# NOTE(x1600): the member is named bootloader2.ucl, not bootloader.ucl. The
# X1000 convention is that "bootloader.ucl" may only be used when the image
# loads at 0x80004000 so that old jztool builds can boot it; the X1600
# bootloader loads at 0x80100000 (see firmware/export/x1600.h), so it must use
# the bootloader2.ucl name to make old tools refuse it rather than load it to
# the wrong address.
# usbstage1 IS a member: jz_x1600_boot() needs it and refuses the package
# without it. It is NOT spl.r1 -- that is the flash image, carrying a 2048-byte
# signature+key header and linked at 0x80001800, which the BootROM cannot load
# over USB. Raw, because stage1 is sent straight to 0x8000a000 and executed.

$(BUILDDIR)/usbstage1$(suffix $(BINARY)): $(BUILDDIR)/usbstage1.bin
	$(call PRINTS,CP $(@F))cp $< $@

$(BUILDDIR)/$(BINARY): $(BUILDDIR)/$(SPLBINARY) \
					   $(BUILDDIR)/usbstage1$(suffix $(BINARY)) \
					   $(BUILDDIR)/bootloader2.ucl \
					   $(BLINFO)
	$(call PRINTS,TAR $(@F))tar -C $(BUILDDIR) \
		--numeric-owner --no-acls --no-xattrs --no-selinux \
		--mode=0644 --owner=0 --group=0 \
		-cf $@ $(call full_path_subst,$(BUILDDIR)/%,%,$^)

$(BUILDDIR)/bootloader2.ucl: $(BUILDDIR)/bootloader.ucl
	$(call PRINTS,CP $(@F))cp $< $@
