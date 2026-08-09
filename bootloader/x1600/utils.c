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

/* Buttons, confirmation prompts, card presence and the cache probes.
 * Mirrors bootloader/x1000/utils.c. */

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

/* Maintained by get_button() in the shared gui, and read by the USB screens.
 * Defined here for the same reason bootloader/x1000/utils.c defines them. */
bool is_usb_connected = false;
intptr_t usb_connection_seqnum = 0;


/* Block until every button is released, so one physical press is never read
 * twice.  Everything here is edge-triggered off this. */
void wait_btn_release(void)
{
    while(read_btn() != 0)
        sleep(HZ/20);
}

int wait_btn_press(void)
{
    int btn;
    do {
        btn = read_btn();
        if(btn == 0)
            sleep(HZ/20);
    } while(btn == 0);
    return btn;
}

/* Returns true if the user confirms.  Used for the two destructive actions. */
bool confirm(const char* what, const char* consequence)
{
    init_lcd();
    wait_btn_release();
    lcd_clear_display();
    int line = 0;
    lcd_putsf(0, line++, "%s?", what);
    line++;
    lcd_putsf(0, line++, "%s", consequence);
    line++;
    lcd_putsf(0, line++, "%s = yes", BL_SELECT_NAME);
    lcd_putsf(0, line++, "%s = no",  BL_QUIT_NAME);
    lcd_update();

    while(1) {
        int btn = wait_btn_press();
        if(btn & BL_SELECT) return true;
        if(btn & BL_QUIT)   return false;
        wait_btn_release();
    }
}

/* Every action reads or writes a file on the card, so refuse early with
 * something readable rather than letting the installer come back with
 * "File not found" when the real problem is that there is no card.
 * X1000's bootloader_action() opens with the same check via check_disk(). */
bool card_available(void)
{
    if(storage_present(IF_MD(0)))
        return true;

    init_lcd();
    lcd_clear_display();
    lcd_putsf(0, 0, "No SD card detected.");
    lcd_putsf(0, 2, "Recovery reads and writes");
    lcd_putsf(0, 3, "files on the card.");
    lcd_putsf(0, 5, "%s to continue", BL_QUIT_NAME);
    lcd_update();
    wait_btn_release();
    while(!(wait_btn_press() & BL_QUIT))
        wait_btn_release();
    return false;
}


