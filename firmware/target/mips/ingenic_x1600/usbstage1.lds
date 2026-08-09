/* Linker script for an X1600 USB stage1 payload; read firmware/export/x1600.h
 * first.
 *
 * The BootROM is STILL RUNNING when a stage1 payload executes, and must survive
 * for the payload to `jr ra` back into it -- which is the whole point of
 * stage1. So ORIGIN sits above its reserved window, LENGTH is the PM's 20 KiB
 * cap, and the ASSERT below refuses to produce an image landing in that window.
 * Loading the vendor SPL image at 0x80000800 hung a device: the bulk transfer
 * died at exactly 2048 bytes and only a power cycle recovered it. Do not
 * remove the ASSERT.
 *
 * A raw binary needs `objcopy -O binary -j .text --change-section-lma .text=0`;
 * a plain `objcopy -O binary` pads from file offset 0x8000a000. */

#include "config.h"
#include "x1600.h"

OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(MIPS)
ENTRY(_usb_stage1_start)
STARTUP(target/mips/ingenic_x1600/spl-start.o)

MEMORY {
    STAGE1 : ORIGIN = X1600_USB_STAGE1_ADDR,
             LENGTH = X1600_USB_STAGE1_MAXSIZE
}

SECTIONS
{
    .text :
    {
        *(.startup.usbstage1);
        *(.text*);
        *(.icode*);
        *(.init*);
        . = ALIGN(4);
        *(.rodata*);
        . = ALIGN(4);
        *(.data*);
        *(.sdata*);
    } > STAGE1

    . = ALIGN(4);
    .bss (NOLOAD) :
    {
        _bssbegin = .;
        *(.sbss*);
        *(.bss*);
        *(COMMON);
        *(.scommon*);
        _bssend = .;
    } > STAGE1

    /DISCARD/ :
    {
        *(.startup.spl);
        *(.MIPS.abiflags);
        *(.eh_frame);
        *(.rel.dyn);
    }

}

/* These ASSERTs must live OUTSIDE the SECTIONS block: inside it, a bare
 * ASSERT(cond, "msg"); is not valid GNU ld syntax and fails with a bare
 * "syntax error" pointing at the message line */

ASSERT(ADDR(.text) >= X1600_BOOTROM_RESERVED_END,
       "USB stage1 must be linked above the BootROM reserved window");
ASSERT(_bssend - ADDR(.text) <= X1600_USB_STAGE1_MAXSIZE,
       "USB stage1 exceeds the BootROM 20 KiB limit (PM 34.9)");
ASSERT(_bssend <= X1600_USB_STAGE1_STACK,
       "USB stage1 collides with its own private stack at 0x8000e000");

