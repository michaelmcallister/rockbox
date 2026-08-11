/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2021 Aidan MacDonald
 * Copyright (C) 2026 Michael McAllister
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

#ifndef __MSC_INGENIC_H__
#define __MSC_INGENIC_H__

#include <stdint.h>

/* Number of MSC controllers on the SOC */
#define MSC_COUNT 2

/* Bus clock rates, in Hz */
#define MSC_SPEED_INIT  400000
#define MSC_SPEED_FAST  25000000
#define MSC_SPEED_HIGH  50000000

/* Per-controller board configuration, defined by the target.  The table is
 * terminated by an entry with msc_nr = -1; controllers which do not appear
 * in it are left alone.
 */
typedef struct msc_config {
    int msc_nr;

    /* STORAGE_SD or STORAGE_MMC */
    int storage_type;

    /* Supported voltages, bus widths and clock speeds; SDMMC_BUS_* */
    uint32_t bus_voltages;
    uint32_t bus_widths;
    uint32_t bus_clocks;

    /* Card detect pin, or GPIO_NONE if the medium is not removable */
    int cd_gpio;
    int cd_active_level;
} msc_config;

extern const msc_config msc_configs[];

/* SOC glue, implemented by the target.
 *
 * msc_soc_init_clock() selects the clock source feeding the controller.
 * msc_soc_set_clock() programs the divider for a bus clock of at most
 * `rate` Hz and returns the frequency it actually gave the controller;
 * the driver divides that down further with MSC_CLKRT.
 */
extern void msc_soc_init_clock(int msc);
extern unsigned msc_soc_set_clock(int msc, unsigned rate);

#endif /* __MSC_INGENIC_H__ */
