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
 *
 ****************************************************************************/

/* HiBy R1 bootloader entry point, split as bootloader/x1000/ is.
 *
 * No OF boot: the R1's original firmware is an A/B pair selected by a flag in
 * mtd5 rather than sitting at a fixed offset, so booting it is an installer
 * operation -- write ota:kernel to mtd5 and reboot -- not a different constant. */

#include "x1600bootloader.h"
#include "ingenic/gui-ingenic.h"
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
#include "clk-x1600.h"

#include <stdbool.h>

#define BLINK_CODE_STORAGE_FAIL   2
#define BLINK_CODE_BOOT_RETURNED  3

void main(void)
{
    bl_beacon(BL_ST_ENTRY);
    system_init();
    bl_beacon(BL_ST_SYSTEM);
    core_allocator_init();
    bl_beacon(BL_ST_ALLOC);
    kernel_init();
    bl_beacon(BL_ST_KERNEL);
    i2c_init();
    bl_beacon(BL_ST_I2C);
    power_init();

    /* Read the rail-enable register back rather than trusting the writes */
    bl_pmu_en = i2c_reg_read1(AXP_PMU_BUS, AXP_PMU_ADDR, 0x90);

    bl_beacon(BL_ST_POWER);

    /* The keys are an ADC ladder, so button_init() needs the SADC up. After
     * power_init(): the ladder is referenced to a PMU rail */
    adc_init();
    bl_beacon(BL_ST_ADC);
    button_init();
#ifdef HAVE_TOUCHSCREEN
    touchscreen_set_mode(TOUCHSCREEN_BUTTON);
#endif
    bl_beacon(BL_ST_BUTTON);
    enable_irq();

    /* Its own stage: died-before-interrupts and died-after are different bugs */
    bl_beacon(BL_ST_IRQ);

    /* Is the tick advancing? Everything after this that can block is rescued
     * only by a tick-driven timeout. Bounded by iterations, not time -- time is
     * the thing in question */
    {
        long t0 = current_tick;
        uint32_t spins = 0;
        while(current_tick == t0 && ++spins < 20000000u)
            ;
        bl_tick_probe = (current_tick != t0) ? spins : 0xFFFFFFFFu;
    }
    bl_beacon(BL_ST_IRQ);   /* re-stamp so b[5] lands in the beacon */


    if(storage_init() < 0) {
        /* Unlike the X1000 this does not power_off(): over a USB stage2 boot a
         * powered-off device needs physical access to recover, where a spinning
         * one comes back on a USB port reset. Drawn by hand rather than with
         * splashf(), which sleeps. */
        bl_beacon(BL_ST_STORAGE_FAIL);
        init_lcd();
        lcd_clear_display();
        lcd_putsf(0, 0, "Storage init failed.");
        lcd_putsf(0, 2, "The SD controller did not");
        lcd_putsf(0, 3, "come up. Recovery needs it");
        lcd_putsf(0, 4, "too, so there is nothing");
        lcd_putsf(0, 5, "useful to offer here.");
        lcd_update();

        while(1);
    }

    bl_beacon(BL_ST_STORAGE);
    filesystem_init();

    bl_beacon(BL_ST_FILESYSTEM);
    usb_init();
    usb_start_monitoring();

    /* Mounting nothing is fine; disk access is guarded by a card check.
     * The R1's microSD is on MSC1 -- MSC0 is the WiFi SDIO slot -- and a
     * factory-formatted card is exFAT, which Rockbox cannot read */
    bl_beacon(BL_ST_USB);

    /* Kept for the beacon: no card, card present but unmountable, and ready to
     * load all look identical from outside otherwise */
    bl_mounts   = disk_mount_all();
    bl_sd_state = sd_present(IF_MD(0)) ? 1 : 0;

    bl_beacon(BL_ST_MOUNT);



    int btn = read_btn();
    btn &= ~BUTTON_POWER;   /* ignore power, it is held during a cold start */
    bl_beacon(BL_ST_BTN);

    /* Enter recovery on a held button OR on a USB boot.
     *
     * The second is what X1000 does -- "If USB booting, the user probably needs
     * to enter recovery mode; let's not force them to hold down the recovery
     * key" (bootloader/x1000/main.c) -- and it matters more here than there.
     * A USB boot has no power-on event, so the button has to be down at the
     * exact instant the host issues PROGRAM_START2. That is the one genuinely
     * fragile step in the whole install procedure, and this removes it.
     *
     * X1000's SPL sets the flag after reading boot_sel; the X1600 equivalent
     * address is unknown, so the USB cache shim sets it instead -- it only ever
     * runs on that path, so it knows. See utils/jztool/bringup/cacheshim.S.
     *
     * NOTE(x1600): BL_RECOVERY is BUTTON_NEXT, which is also the BootROM's USB
     * boot strap.  The two do not collide: holding NEXT with USB attached never
     * reaches this code, because the BootROM claims the boot first.  Holding
     * NEXT on battery is what lands here. */
    if(btn == BL_RECOVERY || get_boot_flag(BOOT_FLAG_USB_BOOT)) {
        clr_boot_flag(BOOT_FLAG_USB_BOOT);   /* one-shot; do not stick */
        recovery_menu();    /* returns when the user picks Exit */
    }

    bl_beacon(BL_ST_BOOT);
    boot_rockbox();

    /* boot_rockbox() only returns on failure -- it does not return on success,
     * having overwritten this code with the Rockbox image.
     *
     * Offer recovery rather than dying silently.  Every likely cause is
     * something the user can act on: no card, no /.rockbox/rockbox.r1, an exFAT
     * card that would not mount, or a bad checksum.  Say so, then let them into
     * the menu -- which is also the only route to Restore if a previous install
     * is what broke the boot. */
    bl_beacon(BL_ST_BOOT_RETURNED);
    init_lcd();
    lcd_clear_display();
    lcd_putsf(0, 0, "Could not start Rockbox.");
    lcd_putsf(0, 2, "Looked for %s", BOOTDIR "/" BOOTFILE);
    lcd_putsf(0, 3, "on a FAT32 card. exFAT will");
    lcd_putsf(0, 4, "not mount.");
    lcd_putsf(0, 6, "%s = recovery options", BL_SELECT_NAME);
    lcd_update();

    wait_btn_release();
    while(1) {
        if(wait_btn_press() & BL_SELECT) {
            recovery_menu();
            /* Retry the boot once the user has had a chance to fix things. */
            bl_beacon(BL_ST_BOOT);
            boot_rockbox();
            bl_beacon(BL_ST_BOOT_RETURNED);
        }
        wait_btn_release();
    }
}
