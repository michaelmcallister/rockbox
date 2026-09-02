/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 * Derived from src/x1000.c, Copyright (C) 2021 Aidan MacDonald
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

/* X1600 USB recovery boot.
 *
 * The flash SPL cannot double as the USB stage1 payload the way it does on the
 * X1000. The X1600 has no TCSM, and while USB boot is live its BootROM runs
 * from locked cache occupying 0x80000000-0x80009000 (PM 34.3) -- which is
 * where the flash SPL is linked. So a separate stage1, linked at 0x8000a000 by
 * firmware/target/mips/ingenic_x1600/usbstage1.lds, is uploaded instead. It is
 * a raw binary with no header, so load address == exec address.
 */

#include "jztool.h"
#include "jztool_private.h"
#include "microtar-stdio.h"
#include <string.h>

static int run_stage1(jz_usbdev* dev, const jz_cpu_info* cpu_info,
                      jz_buffer* buf)
{
    /* Flush first: the BootROM downloads into D-cache and PROGRAM_START1 flips
     * those lines into I-cache, so a stale earlier stage1 would run instead. */
    int rc = jz_usb_flush_caches(dev);
    if(rc < 0)
        return rc;

    rc = jz_usb_send(dev, cpu_info->stage1_load_addr, buf->size, buf->data);
    if(rc < 0)
        return rc;

    return jz_usb_start1(dev, cpu_info->stage1_exec_addr);
}

static int run_stage2(jz_usbdev* dev, const jz_cpu_info* cpu_info,
                      jz_buffer* buf)
{
    /* No "LOAD" header search as on the X1000; no X1600 bootloader carries one.
     * Flush on both sides of the send so a stale I-cache line from an earlier
     * payload at the same address cannot run in its place. */
    int rc = jz_usb_flush_caches(dev);
    if(rc < 0)
        return rc;

    rc = jz_usb_send(dev, cpu_info->stage2_load_addr, buf->size, buf->data);
    if(rc < 0)
        return rc;

    rc = jz_usb_flush_caches(dev);
    if(rc < 0)
        return rc;

    return jz_usb_start2(dev, cpu_info->stage2_exec_addr);
}

/** \brief Load the Rockbox bootloader on an X1600 device
 * \param dev       USB device freshly returned by jz_usb_open()
 * \param type      Device type, used to pick the file extension
 * \param filename  Path to the "bootloader.<ext>" update package
 * \return either JZ_SUCCESS or an error code
 */
int jz_x1600_boot(jz_usbdev* dev, jz_device_type type, const char* filename)
{
    const jz_device_info* dev_info;
    const jz_cpu_info* cpu_info;
    char stage1_filename[32];
    jz_buffer* stage1 = NULL, *bootloader = NULL, *info_file = NULL;
    mtar_t tar;
    int rc;

    dev_info = jz_get_device_info(type);
    if(!dev_info)
        return JZ_ERR_OTHER;

    cpu_info = jz_get_cpu_info(dev_info->cpu_type);
    if(!cpu_info)
        return JZ_ERR_OTHER;

    /* Not "spl.<ext>": that member is the flash image, linked at 0x80001800
     * behind a 2048-byte header, and is not USB-loadable on this SoC. */
    sprintf(stage1_filename, "usbstage1.%s", dev_info->file_ext);

    rc = mtar_open(&tar, filename, "rb");
    if(rc != MTAR_ESUCCESS) {
        jz_log(dev->jz, JZ_LOG_ERROR, "cannot open file %s (tar error: %d)",
               filename, rc);
        return JZ_ERR_OPEN_FILE;
    }

    rc = jz_boot_get_file(dev->jz, &tar, stage1_filename, 0, &stage1);
    if(rc != JZ_SUCCESS) {
        jz_log(dev->jz, JZ_LOG_ERROR,
               "This update package has no %s member, so it cannot be USB "
               "booted. The X1600 needs a stage1 payload linked at 0x%08lx, "
               "separate from the flash SPL.",
               stage1_filename, (unsigned long)cpu_info->stage1_load_addr);
        goto error;
    }

    if(stage1->size > 20 * 1024) {
        jz_log(dev->jz, JZ_LOG_ERROR,
               "stage1 is %zu bytes; the BootROM's limit is 20 KiB (PM 34.9)",
               stage1->size);
        rc = JZ_ERR_BAD_FILE_FORMAT;
        goto error;
    }

    /* Only bootloader2.ucl: the bootloader.ucl name is reserved for images
     * loading at 0x80004000, which on this SoC is inside the BootROM's cache
     * window. */
    rc = jz_boot_get_file(dev->jz, &tar, "bootloader2.ucl", JZ_BOOT_DECOMPRESS, &bootloader);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_boot_get_file(dev->jz, &tar, "bootloader-info.txt", 0, &info_file);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_boot_show_version(dev->jz, info_file);
    if(rc != JZ_SUCCESS)
        goto error;

    /* Stage1 boot to set up clocks and DRAM */
    rc = run_stage1(dev, cpu_info, stage1);
    if(rc != JZ_SUCCESS)
        goto error;

    /* Need a bit of time for stage1 to bring up DRAM */
    jz_sleepms(500);

    /* Stage2 boot into the bootloader's recovery menu */
    rc = run_stage2(dev, cpu_info, bootloader);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = JZ_SUCCESS;

  error:
    if(stage1)
        jz_buffer_free(stage1);
    if(bootloader)
        jz_buffer_free(bootloader);
    if(info_file)
        jz_buffer_free(info_file);
    mtar_close(&tar);
    return rc;
}
