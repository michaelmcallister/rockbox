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

#ifndef __DEBUG_INGENIC_H__
#define __DEBUG_INGENIC_H__

#include <stdbool.h>

/* The debug screens print SoC registers so they are per target, but the menu
 * listing them is not.  Each target supplies the table and its length; the
 * count is explicit because an extern array has no size for ARRAYLEN(). */
struct ingenic_debug_menuitem {
    const char* name;
    bool (*function)(void);
};

extern const struct ingenic_debug_menuitem ingenic_debug_menu[];
extern const int ingenic_debug_menu_count;

#endif /* __DEBUG_INGENIC_H__ */
