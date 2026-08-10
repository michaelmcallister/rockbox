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

/* Panel bring-up. The screen primitives are shared with the X1000 in
 * bootloader/ingenic/gui-ingenic.c; only these two are per target, because this
 * one raises a beacon at each step. */

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
#include "backlight-target.h"
#include "font.h"
#include "i2c-ingenic.h"
#include "i2c-target.h"
#include "dpu-x1600.h"
#include "x1600/intc.h"   /* REG_INTC_* for the INTC diagnostic */
#include "x1600/msc.h"    /* REG_MSC_*  for the storage diagnostic */
#include "clk-x1600.h"    /* clk_get() -- is the MSC clock even alive? */
#include "ingenic/gui-ingenic.h"

bool lcd_ready = false;

/* Bring the display up on demand. Lazy and guarded like the X1000's: a normal
 * boot never powers the panel from here, it goes straight into Rockbox.
 *
 * Clear and push a frame BEFORE the backlight, or the panel lights up showing
 * whatever was in memory. Nothing errors if lcd_init() is skipped -- the draw
 * calls all link and run -- so a missing init shows up only as a blank screen.
 */
void init_lcd(void)
{
    if(lcd_ready)
        return;

    lcd_init();
    bl_beacon(BL_ST_LCD_INIT);
    font_init();
    lcd_setfont(FONT_SYSFIXED);

    lcd_clear_display();
    lcd_update();
    bl_beacon(BL_ST_LCD_FIRST_UPD);

    backlight_init();
    bl_beacon(BL_ST_BACKLIGHT);

    lcd_ready = true;

}

void gui_shutdown(void)
{
    if(!lcd_ready)
        return;

    backlight_hw_off();
}
