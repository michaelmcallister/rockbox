/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
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

#include "system.h"
#include "kernel.h"
#include "lcd.h"
#include "font.h"
#include "action.h"
#include "list.h"
#include "screens.h"
#include "viewport.h"
#include "debug-ingenic.h"

#ifdef INGENIC_CPUIDLE_STATS
bool dbg_cpuidle(void)
{
    do {
        lcd_clear_display();
        lcd_putsf(0, 0, "CPU idle time: %d.%01d%%",
                  __cpu_idle_cur/10, __cpu_idle_cur%10);
        lcd_putsf(0, 1, "CPU frequency: %d.%03d MHz",
                  FREQ/1000000, (FREQ%1000000)/1000);
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}
#endif

static int hw_info_menu_action_cb(int btn, struct gui_synclist* lists)
{
    if(btn == ACTION_STD_OK) {
        int sel = gui_synclist_get_sel_pos(lists);
        FOR_NB_SCREENS(i)
            viewportmanager_theme_enable(i, false, NULL);

        lcd_setfont(FONT_SYSFIXED);
        lcd_set_foreground(LCD_WHITE);
        lcd_set_background(LCD_BLACK);

        if(ingenic_debug_menu[sel].function())
            btn = SYS_USB_CONNECTED;
        else
            btn = ACTION_REDRAW;

        lcd_setfont(FONT_UI);

        FOR_NB_SCREENS(i)
            viewportmanager_theme_undo(i, false);
    }

    return btn;
}

static const char* hw_info_menu_get_name(int item, void* data,
                                         char* buffer, size_t buffer_len)
{
    (void)buffer;
    (void)buffer_len;
    (void)data;
    return ingenic_debug_menu[item].name;
}

bool dbg_hw_info(void)
{
    struct simplelist_info info;
    simplelist_info_init(&info, MODEL_NAME " debug menu",
                         ingenic_debug_menu_count, NULL);
    info.action_callback = hw_info_menu_action_cb;
    info.get_name = hw_info_menu_get_name;
    return simplelist_show_list(&info);
}
