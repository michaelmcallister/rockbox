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

#ifndef __MSC_X1600_H__
#define __MSC_X1600_H__

/* Self-contained: a header that is reached without config.h and tests a
 * config value by VALUE compiles as 0 silently. */
#include "config.h"
#include "x1600/msc.h"
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

    /* Card power switch, or GPIO_NONE if the slot is permanently powered.
     * The driver drives it from set_power_enabled(), so a board with one
     * gets a real power cycle out of sdmmc_host's error recovery rather
     * than just a controller reset.
     *
     * R1: PC25, active high (the vendor device tree's msc1 node carries
     * msc1_pwr_enable_level = 1).  Note PC25 is one of the three pads whose
     * pull resistor points DOWN rather than up (X1600 PM 21.4.2.19), so the
     * pin must actually be driven, not merely released. */
    int pwr_gpio;
    int pwr_active_level;

    /* Pin group carrying clk/cmd/d0..d3, applied by the driver so that the
     * MSC mux is guaranteed to be right even if gpio_init()'s table is later
     * reorganized.  R1/MSC1: port D, pins PD00-PD05, device function 0
     * (X1600 PM Table 21-5, line 19662: PD00 msc1_clk_o, PD01 msc1_cmd,
     * PD02..PD05 msc1_d0..d3, all Fun_0). */
    int pin_port;
    uint32_t pin_mask;
    int pin_func;
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

#endif /* __MSC_X1600_H__ */
