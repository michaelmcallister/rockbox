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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

/* Cache maintenance for the X1600, which unlike every other MIPS target Rockbox
 * supports has an L2. mmu-mips.c excludes its generic versions with
 * #if CONFIG_CPU != X1600.
 *
 * The asm helpers below mirror mmu-mips.c, which is the original if they ever
 * diverge */

#include "config.h"
#include "mips.h"
#include "mipsregs.h"
#include "system.h"
#include "mmu-mips.h"

/* Writeback whole D-cache: no index-type writeback-only op exists. An alias
 * must share a translation unit with its target */
void commit_dcache(void) __attribute__((alias("commit_discard_dcache")));

#define SYNC_WB() __asm__ __volatile__ ("sync":::"memory")

#define INVALIDATE_BTB()                     \
do {                                         \
        register unsigned long tmp;          \
        __asm__ __volatile__(                \
        "    .set push            \n"        \
        "    .set noreorder       \n"        \
        "    .set mips32          \n"        \
        "    mfc0 %0, $16, 7      \n"        \
        "    ssnop                \n"        \
        "    ori  %0, 2           \n"        \
        "    mtc0 %0, $16, 7      \n"        \
        "    ssnop                \n"        \
        "    .set pop             \n"        \
        : "=&r"(tmp));                       \
    } while (0)

#define __CACHE_OP(op, addr)                 \
    __asm__ __volatile__(                    \
    "    .set    push\n\t         \n"        \
    "    .set    noreorder        \n"        \
    "    .set    mips32\n\t       \n"        \
    "    cache   %0, %1           \n"        \
    "    .set    pop              \n"        \
    :                                        \
    : "i" (op), "m"(*(unsigned char *)(addr)))


/* The X1600 has a 128 KiB unified L2 between L1 and DRAM, so maintaining L1
 * alone leaves the data in it.
 *
 * The sequence follows the vendor kernel's r4k_dma_cache_wback_inv:
 *   1. cache 0x15 (DCHitWBInv) over the range, 32-byte stride   -- L1
 *   2. cache 0x17 (SDHitWBInv, secondary) at the L2 line size   -- L2
 *   3. a dummy KSEG1 read to drain the write buffer, then sync  -- barrier
 *
 * Above a size threshold it flushes each cache whole rather than walking the
 * range, as the vendor does: walking a 768 KB framebuffer line by line costs
 * more than flushing 128 KiB.
 *
 * NOT for the X1000, hence the guard in mmu-mips.c: its L1-only path is what
 * three shipping ports depend on. */

#define SDHitWBInv        ((K_CacheHitWBInv   << S_CacheFunc) | K_CacheSecU)
#define SDHitInv          ((K_CacheHitInv     << S_CacheFunc) | K_CacheSecU)
#define SDIndexWBInv      ((K_CacheIndexWBInv << S_CacheFunc) | K_CacheSecU)

#define X1600_L1D_SIZE    (16 * 1024)   /* Config1: 64 sets x 8 ways x 32 B */
#define X1600_L2_SIZE     (128 * 1024)  /* Config2 = 0x80001347             */
#define X1600_L2_LINE     32

/* Whole-cache flush, both levels. The generic version walks L1 only, which
 * publishes nothing here -- large transfers fall back to this path, so an
 * L1-only flush leaves them unpublished. */
static void x1600_commit_discard_all(void)
{
    unsigned int i;

    for(i = A_K0BASE; i < A_K0BASE + CACHE_SIZE; i += CACHEALIGN_SIZE)
        __CACHE_OP(DCIndexWBInv, i);            /* L1 */
    for(i = A_K0BASE; i < A_K0BASE + X1600_L2_SIZE; i += X1600_L2_LINE)
        __CACHE_OP(SDIndexWBInv, i);            /* L2 */

    SYNC_WB();
}

/* The vendor's barrier. A plain `sync` is not enough on its own here -- the
 * uncached read is what forces the write buffer out. */
static inline void x1600_cache_barrier(void)
{
    (void)*(volatile unsigned long*)0xa0000000;
    SYNC_WB();
}

/* Whole-cache, both levels. The generic version below walks L1 only, which
 * publishes nothing here. ⚠ Index-form ops are NOT verified on this part the
 * way the hit forms are -- measure them before trusting this for anything that
 * matters. Nothing on the framebuffer path uses it any more. */
void commit_discard_dcache(void)
{
    x1600_commit_discard_all();
    x1600_cache_barrier();
}

void commit_discard_dcache_range(const void *base, unsigned int size)
{
    /* No whole-cache shortcut here, deliberately: it would use the index-form
     * ops, and only the hit-form pair (0x15 + 0x17) is known to work on this
     * part. Walking 768 KB is more work than a whole-cache flush, but it is the
     * form that publishes. There was a threshold here once and it left the
     * bottom of the panel stale; restore it only once the index forms have been
     * measured the way the hit forms were. */
    char *ptr = CACHEALIGN_DOWN((char*)base);
    char *end = CACHEALIGN_UP((char*)base + size);

    for(char *p = ptr; p != end; p += CACHEALIGN_SIZE)
        __CACHE_OP(DCHitWBInv, p);          /* L1 */
    for(char *p = ptr; p != end; p += X1600_L2_LINE)
        __CACHE_OP(SDHitWBInv, p);          /* L2 */

    x1600_cache_barrier();
}

/* Invalidate so a later read refills from DRAM. Both levels, or L1 refills
 * from a stale L2 line. Linux's r4k_dma_cache_inv calls bcache_ops[3] =
 * mips_sc_inv for exactly this. Edges are written back rather than dropped, as
 * the generic version does, because a partial line may hold live CPU data. */
void discard_dcache_range(const void *base, unsigned int size)
{
    /* No whole-cache shortcut -- see commit_discard_dcache_range(). */
    char *ptr = CACHEALIGN_DOWN((char*)base);
    char *end = CACHEALIGN_UP((char*)base + size);

    if(base != ptr) {
        __CACHE_OP(DCHitWBInv, ptr);
        __CACHE_OP(SDHitWBInv, ptr);
        ptr += CACHEALIGN_SIZE;
    }
    if(ptr != end && (end != ((char*)base + size))) {
        end -= CACHEALIGN_SIZE;
        __CACHE_OP(DCHitWBInv, end);
        __CACHE_OP(SDHitWBInv, end);
    }

    for(char *p = ptr; p != end; p += CACHEALIGN_SIZE)
        __CACHE_OP(DCHitInv, p);            /* L1 */
    for(char *p = ptr; p != end; p += X1600_L2_LINE)
        __CACHE_OP(SDHitInv, p);            /* L2 */

    x1600_cache_barrier();
}

void commit_dcache_range(const void *base, unsigned int size)
{
    /* NOT DCHitWB. The vendor kernel never issues 0x19 anywhere -- verified
     * across 16 KiB of its cache code -- and our build emitting it is why this
     * function was a no-op. WBInv also drops the line, which is a colder cache
     * but correct, and is exactly the trade Linux makes for DMA. */
    commit_discard_dcache_range(base, size);
}


/* Invalidate whole I-cache, both levels. The hierarchy is inclusive, so
 * invalidating L1-I alone achieves nothing -- the refill pulls the stale line
 * back out of L2. Index forms, because there is no whole-cache hit form. */static void discard_icache(void)
{
    register unsigned int i;

    asm volatile (".set push\n.set noreorder\n.set mips32\n"
                  "mtc0 $0, $28\n"      /* TagLo */
                  "mtc0 $0, $29\n"      /* TagHi */
                  ".set pop\n");

    for(i = A_K0BASE; i < A_K0BASE + CACHE_SIZE; i += CACHEALIGN_SIZE)
        __CACHE_OP(ICIndexStTag, i);        /* L1-I */
    for(i = A_K0BASE; i < A_K0BASE + X1600_L2_SIZE; i += X1600_L2_LINE)
        __CACHE_OP(SDIndexWBInv, i);        /* L2 -- or the stale line returns */

    x1600_cache_barrier();
    INVALIDATE_BTB();
}

/* Invalidate the entire I-cache
 * and writeback + invalidate the entire D-cache
 */
void commit_discard_idcache(void)
{
    commit_discard_dcache();
    discard_icache();
}
