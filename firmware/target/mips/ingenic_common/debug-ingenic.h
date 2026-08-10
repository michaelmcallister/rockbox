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

/* Hardware debug menu driver, shared by the X1000 and the X1600.
 *
 * The SCREENS are per-target -- they print SoC registers -- but the machinery
 * that lists them is not: dbg_hw_info(), its two simplelist callbacks and
 * dbg_cpuidle() touch no SoC register at all.
 *
 * Each target supplies the table, exactly as it supplies msc_configs[] and
 * gpio_settings[]. */

#ifndef __DEBUG_INGENIC_H__
#define __DEBUG_INGENIC_H__

#include <stdbool.h>

struct ingenic_debug_menuitem {
    const char* name;
    bool (*function)(void);
};

/* Supplied by the target: the screen list, and its length. The count is
 * explicit because an extern array has no size for ARRAYLEN() to read. */
extern const struct ingenic_debug_menuitem ingenic_debug_menu[];
extern const int ingenic_debug_menu_count;

#ifdef INGENIC_CPUIDLE_STATS
/* Reads __cpu_idle_cur, which both targets maintain in their idle hook. */
extern bool dbg_cpuidle(void);
#endif

#endif /* __DEBUG_INGENIC_H__ */
