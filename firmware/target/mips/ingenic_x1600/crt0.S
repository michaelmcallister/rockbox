/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 *
 * Based on firmware/target/mips/ingenic_x1000/crt0.S,
 * Copyright (C) 2021-2022 Aidan MacDonald
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

/* X1600 vs X1000 differences in this file:
 *
 *  1. No TCSM copy.  The X1600's TCSM belongs to the PDMA MCU and is only
 *     mapped at 0xF400xxxx for the MCU (PM Table 18-7); no Rockbox source
 *     places anything in a .tcsm section, so app.lds has no TCSM region and
 *     there is nothing to copy.
 *
 *  2. The cache init loop bounds are unchanged, and that is a deliberate
 *     verified conclusion rather than a copy/paste.  CP0 Config1 read on this
 *     device is 0xBE27139B:
 *
 *          IS = 0b001 -> 64 sets/way      IL = 0b100 -> 32-byte line
 *          IA = 0b111 -> 8 ways           (same three fields for the D-cache)
 *
 *     so both caches are 64 * 32 * 8 = 16384 bytes with 32-byte lines.  A MIPS
 *     Index cache op selects {way, index} from the low address bits, so
 *     stepping a 16 KiB window in 32-byte increments touches all
 *     64 sets * 8 ways = 512 lines of each cache exactly once.  Hence the
 *     0x4000 window and the 32-byte stride below.
 *
 *     (This also means the loop must NOT be shortened to 64*32 = 2 KiB "one
 *     way".  The way bits live above the index bits on XBurst.)
 *
 * Everything else -- section copies, bss/stack clearing, BTB invalidate, the
 * boot-argument save/restore -- is architecture-generic and identical.
 * firmware/target/mips/{exception-mips.S,system-mips.c,mmu-mips.c} apply
 * unchanged; they only use MIPS32r2 CP0 state and the CACHE instruction.
 */

#include "config.h"
#include "mips.h"
#include "bootdata.h"

#if defined(HAVE_DEVICEDATA) && !defined(BOOTLOADER)
#include "devicedata.h"
#endif

    .text
    .extern main
    .extern system_early_init
    .extern _loadaddress
    .global _start

    .set push
    .set mips32
    .set noreorder
    .set noat

    .section .startup.text,"ax",%progbits

_start:
    b       _realstart
    nop

    /* Header entries are 4-byte string labels (not null terminated!) followed
     * by 4-byte values. Header should begin in the first 128 bytes and should
     * be no more than 256 bytes in length. */
_header:
    .ascii  "BEGINHDR"      /* beginning of header */
    .ascii  "LOAD"
    .word   _loadaddress
    .ascii  "ENDH"          /* end of header structure */

#ifndef BOOTLOADER
    /* Multiboot support header; this is not part of the above header. */
    put_boot_data_here
#ifdef HAVE_DEVICEDATA
    put_device_data_here
#endif
#endif

_realstart:

    /* Save bootloader arguments. */
    move    s0, a0
    move    s1, a1
    move    s2, a2
    move    s3, a3


    /* Copy IRAM from BSS to low memory. */
    la      a0, _iramcopy
    la      a1, _iramstart
    la      a2, _iramend
    bal     _copy
    nop

#ifdef HAVE_INIT_ATTR
    /* Copy init code */
    la      a0, _initcopy
    la      a1, _initstart
    la      a2, _initend
    bal     _copy
    nop
#endif

    /* Clear the BSS segment (needed to zero-initialize C static values) */
    la      a0, _bssbegin
    la      a1, _bssend
    bal     _clear
    move    a2, $0

    /* Set stack pointer and clear the stack */
    la      sp, stackend
    la      a0, stackbegin
    li      a2, 0xDEADBEEF
    bal     _clear
    move    a1, sp

    /* Clear the IRQ stack */
    la      k0, _irqstackend
    la      a0, _irqstackbegin
    bal     _clear
    move    a1, k0

    /* Write back D-cache and invalidate I-cache.
     *
     * 16 KiB per cache, 32-byte lines (CP0 Config1 = 0xBE27139B, see the note
     * at the top of this file), so walk 0x80000000 .. 0x80003FE0 in steps of
     * 32.  TagLo/TagHi are zeroed first so that ICIndexStTag writes an invalid
     * tag rather than whatever was left in the tag registers.
     */
    li      v0, 0x80000000
    ori     v1, v0, (0x4000 - 32)
    mtc0    zero, C0_TAGLO
    mtc0    zero, C0_TAGHI
1:
    cache   DCIndexWBInv, 0(v0)
    cache   ICIndexStTag, 0(v0)
    bne     v0, v1, 1b
    addiu   v0, v0, 32

    /* Invalidate BTB.  Config7 bit 1 is the XBurst BTB invalidate. */
    mfc0    v0, C0_Config, 7
    nop
    ori     v0, v0, 2
    mtc0    v0, C0_Config, 7
    nop

    /* Jump to C code */
    jal     system_early_init
    nop

    /* Restore bootloader arguments, jump to main. */
    move    a0, s0
    move    a1, s1
    move    a2, s2
    move    a3, s3


    j       main
    move    ra, zero     /* init backtrace root */

    /* copy(void* src, void* dst, void* dst_end) */
_copy:
    beq     a1, a2, 1f
    addiu   a1, 4
    lw      t0, 0(a0)
    addiu   a0, 4
    b       _copy
    sw      t0, -4(a1)
1:
    jr      ra
    nop

    /* clear(void* dst, void* dst_end, int value) */
_clear:
    beq     a0, a1, 1f
    addiu   a0, 4
    b       _clear
    sw      a2, -4(a0)
1:
    jr      ra
    nop

    .set pop
