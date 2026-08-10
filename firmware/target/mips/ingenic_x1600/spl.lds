/* Linker script for the X1600 flash SPL (SFC / MSC boot); read
 * firmware/export/x1600.h first.
 *
 * This links INSIDE the window the BootROM occupies, which is safe only because
 * on flash boot the BootROM has finished and jumped away. A USB stage1 payload
 * must NOT use this script -- see usbstage1.lds. Mixing the two up wedges the
 * device and needs a physical power cycle.
 *
 * The BootROM loads the whole image to X1600_SPL_LOAD_ADDR and enters the text
 * at +0x800; the 24 KiB LENGTH below is the PM's hard cap. */

#include "config.h"
#include "x1600.h"

OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(MIPS)
ENTRY(_spl_start)
STARTUP(target/mips/ingenic_x1600/spl-start.o)

MEMORY {
    SPLRAM : ORIGIN = X1600_SPL_EXEC_ADDR,
             LENGTH = X1600_SPL_SIZE
}

SECTIONS
{
    .text :
    {
        *(.startup.spl);
        *(.text*);
        *(.icode*);
        *(.init*);
    } > SPLRAM

    . = ALIGN(4);
    .rodata :
    {
        *(.rodata*);
    } > SPLRAM

    . = ALIGN(4);
    .data :
    {
        *(.data*);
        *(.sdata*);
    } > SPLRAM

    . = ALIGN(4);
    .bss (NOLOAD) :
    {
        _bssbegin = .;
        *(.sbss*);
        *(.bss*);
        *(COMMON);
        *(.scommon*);
        _bssend = .;
    } > SPLRAM

    /DISCARD/ :
    {
        *(.startup.usbstage1);
        *(.MIPS.abiflags);
        *(.eh_frame);
        *(.rel.dyn);
    }
}
