/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 The Rockbox contributors
 *
 * Ingenic X1600 / X1600E SoC definitions.
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

#ifndef __X1600_H__
#define __X1600_H__

#include "config.h"

/* Only a 24 MHz external oscillator is documented, and that is what the R1
 * fits */
#if X1600_EXCLK_FREQ != 24000000
# error "Unsupported EXCLK freq"
#endif

/* Boot memory layout -- read this before touching any link address.
 *
 * The X1600 has no TCSM. Where the X1000's SPL runs from TCSM, a separate
 * address space, the X1600 BootROM runs the SPL out of locked CPU cache aliased
 * into KSEG0 at the same addresses DRAM later occupies, and holds
 * 0x80000000-0x80009000 for itself. So there are two link configurations, and
 * conflating them hangs the device:
 *
 *  (a) FLASH SPL. The BootROM has finished, so trampling its window is
 *      harmless. The exec address is not a guess -- it is the only base at
 *      which the vendor SPL disassembles consistently.
 *  (b) USB STAGE1. The BootROM is still live and must survive, so a payload
 *      links above its window, capped at 20 KB. A flash SPL image is not a
 *      valid stage1 payload: uploading one killed the BootROM mid-transfer and
 *      needed a power cycle.
 *
 * UNVERIFIED: the PM's "36KB of cache" disagrees with CP0 Config1's 16 KB each
 * way, and what backs 0x80009000-0x8000ffff is unknown -- but a payload at
 * 0x8000a000 with its stack at 0x8000e000 and scratch at 0x8000f000 runs and
 * reads its results back. Treat the mechanism as unverified, the addresses as
 * tested. */
#define X1600_SPL_LOAD_ADDR         0x80001000
#define X1600_SPL_EXEC_ADDR         0x80001800
/* PM section 34.5: "SPL.text (not more than 24K, and 512 byte aligned)" */
#define X1600_SPL_SIZE              (24 * 1024)

/* End of the region the BootROM reserves for itself while USB boot is live */
#define X1600_BOOTROM_RESERVED_END  0x80009000

/* USB stage1 payload: link address, hard size cap (PM section 34.9 "must not
 * be greater than 20KB"), and the private stack a returning payload uses so it
 * can preserve every BootROM register. See spl-start.S. */
#define X1600_USB_STAGE1_ADDR       0x8000a000
#define X1600_USB_STAGE1_MAXSIZE    (20 * 1024)
#define X1600_USB_STAGE1_STACK      0x8000e000

/* External DRAM: one linear mapping in KSEG0. 64 MiB on the HiBy R1,
 * verified as mem=64M@0x0 / physical 0x00000000-0x03ffffff. */
#define X1600_SDRAM_BASE            0x80000000
#define X1600_SDRAM_SIZE            (MEMORYSIZE * 1024 * 1024)
#define X1600_SDRAM_END             (X1600_SDRAM_BASE + X1600_SDRAM_SIZE)

/* Rockbox IRAM/DRAM split, same shape as the X1000 port: IRAM holds the
 * exception vectors and must start at the base of KSEG0 */
#define X1600_IRAM_BASE             X1600_SDRAM_BASE
#define X1600_IRAM_SIZE             (16 * 1024)
#define X1600_IRAM_END              (X1600_IRAM_BASE + X1600_IRAM_SIZE)
#define X1600_DRAM_BASE             X1600_IRAM_END
#define X1600_DRAM_SIZE             (X1600_SDRAM_SIZE - X1600_IRAM_SIZE)
#define X1600_DRAM_END              (X1600_DRAM_BASE + X1600_DRAM_SIZE)

/* Where the SPL puts the bootloader, and so where it links. Not the X1000's
 * 0x80004000: the running SPL occupies 0x80001800 upwards, so a bootloader
 * there would be copied over the copier. This is above the whole BootROM/SPL
 * cache window, and is what the vendor SPL uses for its own second stage. */
/* Overridable at build time for bring-up experiments only -- relinking to an
 * address nothing has ever executed discriminates a stale-I-cache fault from
 * the alternatives. Do not ship a non-default value: it must match what the
 * SPL decompresses to and what jztool's stage2_load_addr says. */
#ifndef X1600_BOOT_LOAD_ADDR
#define X1600_BOOT_LOAD_ADDR        0x80100000
#endif
#define X1600_BOOT_EXEC_ADDR        X1600_BOOT_LOAD_ADDR

/* Stacks live in IRAM to keep boot code simple, as on the X1000 */
#define X1600_STACKSIZE             0x1e00

/* 0x800, not the X1000's 0x300, and the difference is measured: the deepest
 * tick-task chain is 1296 bytes against the 768 that 0x300 leaves, and a panic
 * in interrupt context costs ~800 on its own through the font machinery. There
 * is no guard page, so an overflow corrupts the very frames the backtrace is
 * trying to report. Upstream's X1000 uses 0x300 with the same panic path and
 * is presumably exposed the same way. */
#define X1600_IRQSTACKSIZE          0x800

/* Convert kseg0 address to physical address or uncached address */
#define PHYSADDR(x)     ((unsigned long)(x) & 0x1fffffff)
#define UNCACHEDADDR(x) (PHYSADDR(x) | 0xa0000000)

/* usb-designware. Both values match the X1000's, and the endpoint count is
 * confirmed by GHWCFG2/3 on the device -- see usb-x1600.c. */
#define OTGBASE             0xb3500000
#define USB_NUM_ENDPOINTS   9

/* CPU cache parameters, read from CP0 Config1 = 0xBE27139B by our own
 * bare-metal code on the device: 64 sets x 32 B line x 8 way = 16 KB each
 * for I- and D-cache, 32-byte lines */
#define CACHEALIGN_BITS     5
#define CACHEALIGN_SIZE     32
#define CACHE_SIZE          (16 * 1024)

#endif /* __X1600_H__ */
