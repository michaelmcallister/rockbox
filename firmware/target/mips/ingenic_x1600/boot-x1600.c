/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> >    <
 *   Player     |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 *
 * Based on firmware/target/mips/ingenic_x1000/boot-x1000.c
 * Copyright (C) 2021 Aidan MacDonald
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

#include "boot-x1600.h"
#include "x1600.h"
#include "system.h"
#include <string.h>

#define HDR_BEGIN   32      /* header must begin within this many bytes */
#define HDR_LEN     64      /* header length cannot exceed this */

/* search for header value, label must be a 4-character string.
 * Returns the found value or 0 if the label wasn't found. */
static uint32_t search_header(const unsigned char* source, size_t length,
                              const char* label)
{
    size_t search_len = MIN(length, (size_t)HDR_BEGIN);
    if(search_len < 8)
        return 0;
    search_len -= 7;

    /* find the beginning marker */
    size_t i;
    for(i = 8; i < search_len; i += 4)
        if(!memcmp(&source[i], "BEGINHDR", 8))
            break;
    if(i >= search_len)
        return 0;
    i += 8;

    /* search within the header */
    search_len = MIN(length, i + HDR_LEN) - 7;
    for(; i < search_len; i += 8) {
        if(!memcmp(&source[i], "ENDH", 4)) {
            break;
        } else if(!memcmp(&source[i], label, 4)) {
            i += 4;
            /* read little-endian value */
            return (uint32_t)source[i]
                 | ((uint32_t)source[i+1] << 8)
                 | ((uint32_t)source[i+2] << 16)
                 | ((uint32_t)source[i+3] << 24);
        }
    }

    return 0;
}

/* Copy the image to its load address, publishing every byte to DRAM.
 *
 * Reads cached, since the source may still be dirty; writes through the KSEG1
 * uncached alias, so nothing is left for a later maintenance op to publish and
 * nothing depends on what the replacement policy evicted. commit_dcache_range()
 * would also work now (see mmu-mips.c); this form has booted. */
static void iram_publish_copy(void* dest, const void* source, size_t length)
    __attribute__((section(".icode")));

static void iram_publish_copy(void* dest, const void* source, size_t length)
{
    const unsigned char* s = source;
    const unsigned char* dc = dest;     /* cached, for the overlap test */
    volatile unsigned char* d = (volatile unsigned char*)UNCACHEDADDR(dest);

    if(s < dc && dc < s + length) {
        d += length;
        s += length;
        while(length--)
            *--d = *--s;
        return;
    }

    /* Word stores when the two agree modulo 4. A megabyte of uncached byte
     * stores is four times the bus traffic for no reason. */
    if((((uintptr_t)s ^ (uintptr_t)d) & 3) == 0) {
        while(length && ((uintptr_t)d & 3)) {
            *d++ = *s++;
            --length;
        }

        /* Via uintptr_t: the peel establishes the alignment, which
         * -Wcast-align cannot see. */
        volatile uint32_t* dw = (volatile uint32_t*)(uintptr_t)d;
        const uint32_t* sw = (const uint32_t*)(uintptr_t)s;
        size_t words = length / 4;
        for(size_t i = 0; i < words; ++i)
            dw[i] = sw[i];

        d += words * 4;
        s += words * 4;
        length -= words * 4;
    }

    while(length--)
        *d++ = *s++;
}

/* Invalidate the I-cache over a range, by address: cache 0x10 (ICHitInv) then
 * 0x13 (SDHitInv) per 32-byte line. Either alone is 0/8 -- the hierarchy is
 * inclusive, so invalidating L1-I alone lets the refill pull the stale line
 * back out of L2. Both together measured 1024/1024 over twice the I-cache.
 *
 * Not commit_discard_idcache(): its discard_icache() is index-form, and the
 * index forms are still unverified here.
 *
 * ⚠ 0x13 invalidates WITHOUT writing back, so this is only safe over a range
 * with no dirty lines -- which the uncached copy above guarantees.
 *
 * Runs from IRAM: it executes after the copy has landed, so it must not be
 * fetched from what the copy overwrote. */
static void iram_discard_icache_range(const void* addr, size_t length)
    __attribute__((section(".icode")));

static void iram_discard_icache_range(const void* addr, size_t length)
{
    uintptr_t line = (uintptr_t)addr & ~31u;
    uintptr_t end  = ((uintptr_t)addr + length + 31u) & ~31u;

    /* Drain first: the copy's stores must reach DRAM before the lines are
     * invalidated, or the refill races ahead of the write buffer. */
    __asm__ __volatile__(".set push\n.set mips32\nsync\n.set pop" ::: "memory");

    for(; line < end; line += 32) {
        __asm__ __volatile__(".set push\n.set noreorder\n.set mips32\n"
                             "cache 0x10, 0(%0)\n"      /* ICHitInv */
                             "cache 0x13, 0(%0)\n"      /* SDHitInv */
                             ".set pop" :: "r"(line) : "memory");
    }

    __asm__ __volatile__(".set push\n.set mips32\nsync\n.set pop" ::: "memory");
}

/* The copy overwrites the bootloader's own text, so this function, its stack
 * and everything it touches must lie below the load address. They do:
 * X1600_DRAM_BASE is X1600_IRAM_END, putting .iram and both stacks in the low
 * 16 KiB. */
void x1600_boot_rockbox(const void* source, size_t length)
{
    uint32_t load_addr = search_header(source, length, "LOAD");
    if(!load_addr)
        load_addr = X1600_DRAM_BASE;

    disable_irq();

    /* --- Beyond this point, do not call into DRAM --- */

    /* Publish, then invalidate -- in that order, and neither is optional. The
     * copy leaves the image in DRAM; the invalidate stops the jump below
     * fetching whatever the outgoing image left in the I-cache at the same
     * addresses, not commit_discard_idcache(), which is index-form. */
    iram_publish_copy((void*)load_addr, source, length);
    iram_discard_icache_range((const void*)load_addr, length);

    typedef void(*entry_fn)(void);
    entry_fn fn = (entry_fn)load_addr;
    fn();
    while(1);
}

/* RoLo. The generic MIPS fallback in firmware/rolo.c ignores the LOAD header
 * and the IRAM-resident copy, so delegate as the X1000 does. */
void rolo_restart(const unsigned char* source, unsigned char* dest, int length)
{
    (void)dest;     /* x1600_boot_rockbox() takes the address from the header */

    /* Both halves live in x1600_boot_rockbox(): the copy publishes through
     * KSEG1, the I-cache is invalidated by address. Do NOT add a read sweep or
     * a Config0.K0 flip here -- both rest on the cache being a no-op, which it
     * is not, and are unsound on their own terms.
     *
     * ⚠ UNTESTED on hardware. Every primitive under it is measured; this
     * composition has never executed.
     *
     * NOT .icode -- it links into DRAM above the load address, so the copy
     * overwrites it. Safe only because x1600_boot_rockbox() never returns. */
    disable_irq();

    x1600_boot_rockbox(source, length);
}
