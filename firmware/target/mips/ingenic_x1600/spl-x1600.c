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

/*
 * X1600 SPL -- the flash-boot second program loader. Never run: nothing has
 * been flashed on this device, and a RAM boot over USB does not come this way.
 *
 * Unlike spl-x1000.c this is not also the USB stage1 payload -- a flash SPL
 * links at 0x80001800, which the BootROM still occupies during a USB boot, so
 * stage1 is a separate binary (usbstage1-x1600.c). Hence no USB-boot detection
 * here: if this is running, it came from flash. There is also no CPM_SCRATCH
 * on this SoC to pass arguments through, and DRAM is x1600_ddr_init() driving
 * an Innosilicon PHY rather than the X1000's Synopsys one.
 */

#include "config.h"
#include "boot-x1600.h"
#include "x1600.h"
#include "system.h"
#include "spl-x1600.h"
#include "ddr-x1600.h"
#include "x1600/cpm.h"
#include "x1600/wdt.h"
#include "x1600/tcu.h"
#include "ost-ingenic.h"
#include "ucl_decompress.h"
#include <string.h>

/* Whether the bootloader image is UCL-compressed (it is, see x1600boot.make) */
#ifndef SPL_USE_UCLPACK
# define SPL_USE_UCLPACK 1
#endif

/* Error indicator: the red LED on PC01, driven directly to keep the SPL's
 * dependencies short. Port bases are 0x10010000 + 0x100 per port; note PXINT
 * sits at 0x10 here, not 0x00 as on the X1000:
 *   PXINT/S/C 0x10/0x14/0x18   PXMSK/S/C 0x20/0x24/0x28
 *   PXPAT1/S/C 0x30/0x34/0x38  PXPAT0/S/C 0x40/0x44/0x48
 * Output is INTC(bit); MSKS(bit); PAT1C(bit), then PAT0S/PAT0C to drive it. */
#define GPIO_PC_BASE    0xb0010200
#define SPL_ERROR_BIT   (1 << 1)        /* PC01, red LED */

static void spl_error_pin_output(void)
{
    *(volatile uint32_t*)(GPIO_PC_BASE + 0x18) = SPL_ERROR_BIT; /* PXINTC  */
    *(volatile uint32_t*)(GPIO_PC_BASE + 0x24) = SPL_ERROR_BIT; /* PXMSKS  */
    *(volatile uint32_t*)(GPIO_PC_BASE + 0x38) = SPL_ERROR_BIT; /* PXPAT1C */
}

static void spl_error_pin_set(int level)
{
    *(volatile uint32_t*)(GPIO_PC_BASE + (level ? 0x44 : 0x48)) = SPL_ERROR_BIT;
}

void spl_error(void)
{
    int level = 0;
    spl_error_pin_output();
    while (1) {
        spl_error_pin_set(level);
        mdelay(100);
        level = 1 - level;
    }
}

/* Blink an error code forever: `code` short flashes, then a long gap. The LED
 * is the SPL's only output on a flash boot, so x1600_ddr_init()'s error enum
 * (ddr-x1600.h) is kept small enough to count by eye; ddr_last_status holds
 * the detail for anyone who can read memory back. */
static void spl_error_code(int code)
{
    int i;

    spl_error_pin_output();
    if (code <= 0)
        spl_error();

    while (1) {
        for (i = 0; i < code; ++i) {
            spl_error_pin_set(1);
            mdelay(200);
            spl_error_pin_set(0);
            mdelay(200);
        }
        mdelay(1500);
    }
}

/* Heap, allocated downwards from the top of DRAM as on the X1000. */
static void* heap = (void*)(X1600_SDRAM_BASE + X1600_SDRAM_SIZE);

void* spl_alloc(size_t count)
{
    heap -= CACHEALIGN_UP(count);
    memset(heap, 0, CACHEALIGN_UP(count));
    return heap;
}

static void* get_load_buffer(void)
{
    /* Read to a scratch buffer if we have to decompress, else read straight
     * to the final load address. */
    if (SPL_USE_UCLPACK)
        return spl_alloc(SPL_BOOT_STORAGE_SIZE);
    else
        return (void*)X1600_BOOT_LOAD_ADDR;
}

void spl_main(void)
{
    int rc;
    void* load_buffer;

    /* Stop the watchdog before anything else. The BootROM leaves it ARMED and
     * counting -- roughly two seconds -- and nothing here ever serviced it.
     * Invisible until the first flashed boot, because a RAM upload never runs
     * the SPL: Rockbox reached its menu and reset, over and over.
     *
     * Both the counter and its clock gate, so it cannot resume if something
     * later ungates the TCU. */
    REG_WDT_ENABLE = 0;             /* stop the counter   */
    jz_set(TCU_STOP, 1u << 16);     /* and gate its clock */

    /* Order mirrors spl-x1000.c: time base, then DRAM, then everything else.
     * The BootROM leaves MPLL disabled, so the DDR clock does not exist yet;
     * x1600_ddr_init() owns that whole path here, because every wait on it has
     * to be bounded and clk_set_ddr() still spins forever.
     *
     * Boot flags first: system_init() skips clk_init() when BOOT_FLAG_CLK_INIT
     * is set, and CPSPR holds whatever a previous boot left. */
    init_boot_flags();

    /* Sample the straps before any driver can re-mux port C -- once the value
     * is in CPSPR it survives into the bootloader and Rockbox, by which time
     * the pins may no longer say. */
    if(x1600_boot_sel() == X1600_BOOT_SEL_USB)
        set_boot_flag(BOOT_FLAG_USB_BOOT);

    init_ost();

    rc = x1600_ddr_init(&x1600_ddr_param_hibyr1);
    if (rc != 0)
        spl_error_code(rc);

    /* clk_init() would be safe here and would take the core to 1104 MHz for
     * the read below. Not done: the SPL is the flashed artifact, and an
     * untested clock change fails every boot with only a blink code to say so.
     * Revisit once the SPL has booted this device at all. */

    rc = spl_storage_open();
    if (rc != 0)
        spl_error();

    load_buffer = get_load_buffer();
    rc = spl_storage_read(SPL_BOOT_STORAGE_ADDR, SPL_BOOT_STORAGE_SIZE,
                          load_buffer);
    if (rc != 0)
        spl_error();

    /* out_size is the whole span to the top of DRAM, as on the X1000. The
     * source buffer is at the top of DRAM (spl_alloc grows down), so a large
     * enough image could in principle decompress over it -- 71 KB against
     * ~63 MB of headroom, so not a real risk. */
    if (SPL_USE_UCLPACK) {
        uint32_t out_size = X1600_SDRAM_END - X1600_BOOT_LOAD_ADDR;
        rc = ucl_unpack((uint8_t*)load_buffer, SPL_BOOT_STORAGE_SIZE,
                        (uint8_t*)X1600_BOOT_LOAD_ADDR, &out_size);
    } else {
        rc = 0;
    }

    if (rc != 0)
        spl_error();

    spl_storage_close();

    /* The bootloader was written through the D-cache and is about to be
     * fetched as instructions, from KSEG0 addresses this code is itself
     * running out of. */
    typedef void(*entry_fn)(void);
    entry_fn fn = (entry_fn)X1600_BOOT_EXEC_ADDR;
    commit_discard_idcache();
    fn();
}
