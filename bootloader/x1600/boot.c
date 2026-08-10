/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 * Structure follows bootloader/x1000/main.c,
 * Copyright (C) 2021 Aidan MacDonald
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/

/* Loading Rockbox and handing control to it.  Mirrors
 * bootloader/x1000/boot.c. */

#include "x1600bootloader.h"
#include "system.h"
#include "core_alloc.h"
#include "kernel/kernel-internal.h"
#include "power.h"
#include "button.h"
#include "adc.h"
#include "storage.h"
#include "disk.h"
#include "file_internal.h"
#include "usb.h"
#include "rb-loader.h"
#include "loader_strerror.h"
#include "boot-x1600.h"
#include "installer-ingenic.h"
#include "lcd.h"
#include "backlight.h"
#include "font.h"
#include "i2c-ingenic.h"
#include "i2c-target.h"
#include "dpu-x1600.h"
#include "x1600/intc.h"   /* REG_INTC_* for the INTC diagnostic */
#include "x1600/msc.h"    /* REG_MSC_*  for the storage diagnostic */
#include "clk-x1600.h"    /* clk_get() -- is the MSC clock even alive? */
#include "ingenic/gui-ingenic.h"

int bl_preloaded_size(void)
{
    const volatile uint32_t* h = (const volatile uint32_t*)BL_PRELOAD_HDR;
    if(h[0] != BL_PRELOAD_MAGIC)
        return 0;
    uint32_t len = h[1];
    /* Sanity: a Rockbox image is megabytes, not gigabytes. Refuse anything that
     * would run off the end of DRAM rather than trusting a header the host may
     * have written incorrectly -- getting this wrong means jumping into
     * whatever happens to be in memory. */
    if(len < 0x1000 || len > (16u << 20))
        return 0;
    return (int)len;
}

void boot_rockbox(void)
{
    size_t size;

    /* Preloaded image takes precedence: it is only present when a host has
     * deliberately put it there for this boot. */
    int pre = bl_preloaded_size();
    if(pre > 0) {
        bl_beacon(BL_ST_BOOT_JUMP);
        x1600_boot_rockbox((const void*)BL_PRELOAD_IMAGE, (size_t)pre);
        /* not reached */
    }

    int handle = core_alloc_maximum(&size, &buflib_ops_locked);
    if(handle < 0) {
        bl_beacon(BL_ST_BOOT_NOMEM);
        return;
    }

    unsigned char* loadbuffer = core_get_data(handle);
    int rc = load_firmware(loadbuffer, BOOTFILE, size);
    if(rc <= 0) {
        /* rc is a LOAD_ERROR_*; loader_strerror() would name it if there were
         * anywhere to print it. */
        core_free(handle);
        bl_beacon(BL_ST_BOOT_LOADFAIL);
        return;
    }

    core_shrink(handle, loadbuffer, rc);
    bl_beacon(BL_ST_BOOT_JUMP);

    /* Does not return: x1600_boot_rockbox() copies the image over the DRAM we
     * are running from and jumps to it. */
    x1600_boot_rockbox(core_get_data(handle), rc);
}
