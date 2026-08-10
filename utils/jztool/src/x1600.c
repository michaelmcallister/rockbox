/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 The Rockbox contributors
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

/*
 * X1600 USB recovery boot.
 *
 * WHY THIS IS NOT src/x1000.c WITH DIFFERENT CONSTANTS
 * ---------------------------------------------------
 * On the X1000, the flash SPL image doubles as the USB stage1 payload. That
 * works because the SPL's 2048-byte header/key region is skipped by loading the
 * whole image at TCSM+0x1000 and executing at TCSM+0x1800, and because TCSM is
 * a separate address space from the BootROM's working memory.
 *
 * The X1600 has no TCSM. Its BootROM runs the SPL out of locked cache aliased
 * into KSEG0 and occupies 0x80000000-0x80009000 itself (PM 34.3) for as long as
 * USB boot is live. The flash SPL is linked at 0x80001800, right in the middle
 * of that. Uploading a flash SPL image to 0x80000800 and starting it at
 * 0x80001000 was tried on hardware: the bulk transfer died at exactly 2048
 * bytes and the BootROM never answered again -- recovery required physically
 * removing power.
 *
 * So the X1600 needs a purpose-built stage1, linked at 0x8000a000 by
 * firmware/target/mips/ingenic_x1600/usbstage1.lds, and that is what this code
 * uploads. It is a raw binary with no header, so load address == exec address.
 *
 * The download path is SET_DATA_ADDRESS, SET_DATA_LENGTH, a bulk OUT in 1024
 * byte chunks, then FLUSH_CACHES. GET_CPU_INFO returns "X1600".
 */

#include "jztool.h"
#include "jztool_private.h"
#include "microtar-stdio.h"
#include <stdbool.h>
#include <string.h>

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

    /* NOT "spl.<ext>". That member of the update package is the *flash* image,
     * complete with its 2048-byte signature+key header, linked at 0x80001800,
     * and it is not loadable over USB on this SoC. */
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

    /* Only "bootloader2.ucl" is accepted. The plain "bootloader.ucl" name is
     * reserved by the X1000 convention for images that load at 0x80004000, and
     * an X1600 bootloader never does -- 0x80004000 is inside the BootROM/SPL
     * cache window. */
    rc = jz_boot_get_file(dev->jz, &tar, "bootloader2.ucl", JZ_BOOT_DECOMPRESS, &bootloader);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_boot_get_file(dev->jz, &tar, "bootloader-info.txt", 0, &info_file);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_boot_show_version(dev->jz, info_file);
    if(rc != JZ_SUCCESS)
        goto error;

    /* Stage1: bring up clocks and DRAM, then return to the BootROM.
     *
     * FLUSH_CACHES first. The BootROM downloads into D-cache and PROGRAM_START1
     * flips those lines into I-cache; a stale I-cache copy of a *previous*
     * stage1 will otherwise re-run instead of the one just uploaded. This was
     * observed on the device while iterating on payloads. */
    rc = jz_usb_flush_caches(dev);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_usb_send(dev, cpu_info->stage1_load_addr, stage1->size, stage1->data);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_usb_start1(dev, cpu_info->stage1_exec_addr);
    if(rc != JZ_SUCCESS)
        goto error;

    /* Give stage1 time to finish DRAM training and return. */
    jz_sleepms(500);

    /* Stage2: the bootloader itself, straight into DRAM.
     *
     * HARDWARE NOTE, unresolved -- read before changing this.
     * Three things are established on the device:
     *   (a) a 96 B stage2 payload sent to 0x80100000 (KSEG0) with a flush does
     *       execute, but reads its own first word back through KSEG1 as
     *       garbage: the BootROM's download sat in D-cache and PROGRAM_START2
     *       flipped it into I-cache, so it ran from cache while DRAM never got
     *       the data.
     *   (b) the same payload sent to the KSEG1 alias 0xa0100000 executes AND
     *       reads itself back correctly -- the image is genuinely in DRAM.
     *   (c) the real 71 KB bootloader sent to 0xa0100000 round-trips byte-exact
     *       but does NOT reach its entry point.
     * The leading explanation for (c) is a STALE I-CACHE: earlier payloads had
     * run at 0x80100000, a KSEG1 send never touches the caches, and a flush
     * issued only AFTER the send may not invalidate those I-cache lines -- so
     * PROGRAM_START2 re-ran an old payload.  Which is exactly the failure the
     * stage1 comment above warns about.
     * Hence: flush BEFORE the send as well as after, and if that fixes it,
     * prefer plain KSEG0 sends (what jz_x1000_boot does) over the KSEG1 trick.
     * scratchpad/bisect_stage2.py runs this experiment and flags a stale run.
     *
     * WHY crt0 CANNOT SAVE US HERE, which is the part that makes this subtle:
     * firmware/target/mips/ingenic_x1600/crt0.S does invalidate both caches --
     * but only around line 158, long after _realstart at line 95.  The stale
     * lines span 0x80100000..0x80100060, the extent of the 96-byte probe that
     * ran there, and _realstart is at 0x8010001c -- INSIDE that range.  So if
     * those lines are stale the CPU never executes crt0 at all, and crt0's own
     * cache init is unreachable.  The invalidation has to happen BEFORE the
     * jump, from code at an address no earlier payload has executed.
     *
     * scratchpad/cacheshim.S does exactly that: loaded at 0x80300000 (virgin,
     * so it cannot itself be served stale), it sweeps 16 KiB of index space
     * doing Index_Writeback_Invalidate_D and Index_Invalidate_I in 32-byte
     * steps, syncs, then jumps to _realstart.  The 16 KiB / 32-byte figures are
     * not guesses: crt0.S records CP0 Config1 = 0xBE27139B on this part, i.e.
     * 64 sets x 8 ways x 32 bytes for both caches.
     *
     * TODO(x1600): cpu_info->stage2_load_addr is a starting guess (see
     * src/device_info.c). It must match the address the bootloader is linked
     * for, X1600_BOOT_LOAD_ADDR in firmware/export/x1600.h. Unlike the X1000
     * path there is no "LOAD" header search here, because no X1600 bootloader
     * binary carries one yet; add it if that changes. */
    rc = jz_usb_flush_caches(dev);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_usb_send(dev, cpu_info->stage2_load_addr,
                     bootloader->size, bootloader->data);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_usb_flush_caches(dev);
    if(rc != JZ_SUCCESS)
        goto error;

    rc = jz_usb_start2(dev, cpu_info->stage2_exec_addr);
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
