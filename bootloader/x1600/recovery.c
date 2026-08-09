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

/* The recovery menu: install, backup and restore.  Mirrors
 * bootloader/x1000/recovery.c + install.c. */

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
#include <string.h>

/* Minimal recovery menu.
 *
 * The X1000 targets expose install/backup/restore through bootloader/x1000/
 * install.c on top of gui.c.  This port has no gui.c, so this draws the same
 * three actions with the primitives the bootloader already links --
 * lcd_clear_display/lcd_putsf/lcd_update and the read_btn() main() uses.
 *
 * RESTORE matters as much as INSTALL: installing erases the vendor SPL, and
 * without a way to put it back the only recovery is BootROM USB on a host.
 * Leaving backup_bootloader()/restore_bootloader() implemented but unreachable
 * -- which is how this stood until now, they were not even linked in -- means
 * the safety net exists on paper only.
 */
enum {
    RA_INSTALL = 0,
    RA_BACKUP,
    RA_RESTORE,
    RA_EXIT,
    RA_COUNT,
};

static const char* const recovery_items[RA_COUNT] = {
    "Install bootloader",
    "Backup bootloader",
    "Restore bootloader",
    "Exit",
};

void run_action(int which)
{
    int rc;

    if(!card_available())
        return;

    switch(which) {
    case RA_INSTALL:
        if(!confirm("Install bootloader", "ERASES the vendor SPL."))
            return;
        bl_beacon(BL_ST_RECOVERY);
        rc = install_bootloader("/bootloader." BOOTFILE_EXT);
        break;
    case RA_BACKUP:
        /* Non-destructive: writes a file, touches no flash. */
        rc = backup_bootloader(BOOTBACKUP_FILE);
        break;
    case RA_RESTORE:
        if(!confirm("Restore bootloader", "OVERWRITES flash from file."))
            return;
        rc = restore_bootloader(BOOTBACKUP_FILE);
        break;
    default:
        return;
    }

    bl_beacon(rc == IERR_SUCCESS ? BL_ST_INSTALL_OK : BL_ST_INSTALL_FAIL);

    lcd_clear_display();
    lcd_putsf(0, 0, "%s", recovery_items[which]);
    /* installer_strerror() names the failure; the raw code is kept alongside it
     * because it is also what the SRAM beacon carries, so a photo of the screen
     * and a beacon read agree. */
    lcd_putsf(0, 1, "%s (%d)", installer_strerror(rc), rc);
    lcd_putsf(0, 3, "%s to continue", BL_QUIT_NAME);
    lcd_update();

    wait_btn_release();
    while(!(wait_btn_press() & BL_QUIT))
        wait_btn_release();
}

static struct bl_list recmenu_list;

/* Same "=> item" idiom as bootloader/x1000/recovery.c, so both players draw
 * the recovery menu identically. */
static void recmenu_draw_item(const struct bl_listitem* item)
{
    const char* fmt = (item->index == item->list->selected_item)
                    ? "=> %s" : "   %s";
    lcd_putsxyf(item->x, item->y, fmt, recovery_items[item->index]);
}


/* Drawn with the shared primitives (clearscreen, putcenter_y, gui_list_*) so
 * the screen matches the X1000's.
 *
 * INPUT IS DELIBERATELY NOT SHARED.  The X1000 reads the kernel button queue
 * via get_button(); this polls the hardware through read_btn() and detects
 * edges itself.  That is not stylistic: the R1's keys are an ADC ladder, this
 * menu "never responded" until adc_init() was added to main(), and the comment
 * recording that fix still says "very likely" -- it has not been confirmed on
 * hardware.  Switching to the button queue would change an input path that has
 * never been observed working, on the one screen that installs the bootloader.
 * Revisit once a device cycle has confirmed the queue delivers ladder keys. */
void recovery_menu(void)
{
    struct viewport vp = {
        .x = 0, .y = SYSFONT_HEIGHT,
        .width = LCD_WIDTH,
        .height = LCD_HEIGHT - SYSFONT_HEIGHT*5,
    };

    init_lcd();
    lcd_init_viewport(&vp);

    struct bl_list* list = &recmenu_list;
    gui_list_init(list, &vp);
    list->draw_item = recmenu_draw_item;
    list->num_items = RA_COUNT;
    gui_list_select(list, 0);

    while(1) {
        clearscreen();
        putcenter_y(0, "Rockbox recovery menu");

        int ypos = LCD_HEIGHT - 4*SYSFONT_HEIGHT;
        put_help_line(ypos, 0, BL_DOWN_NAME "/" BL_UP_NAME, "move cursor");
        put_help_line(ypos, 1, BL_SELECT_NAME, "select item");
        put_help_line(ypos, 2, BL_QUIT_NAME, "exit");

        gui_list_draw(list);
        lcd_update();
        bl_beacon(BL_ST_MENU_DRAWN);

        wait_btn_release();
        bl_beacon(BL_ST_MENU_INPUT);
        int btn = wait_btn_press();

        if(btn & BL_UP)
            gui_list_scroll(list, -1);
        else if(btn & BL_DOWN)
            gui_list_scroll(list, 1);
        else if(btn & BL_SELECT) {
            if(list->selected_item == RA_EXIT)
                return;
            run_action(list->selected_item);
        } else if(btn & BL_QUIT)
            return;
    }
}
