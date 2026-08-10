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

/* Board and SoC glue for the shared MSC driver (ingenic_common/msc-ingenic.c).
 *
 * Both controllers take their clock parent from MSC0CDR.CLKSRC -- MSC1CDR has
 * no source field of its own on this part, which is the one place it differs
 * from the X1600.
 */

#include "system.h"
#include "ingenic-soc.h"

const msc_config msc_configs[] = {
#if defined(FIIO_M3K)
#define MSC_CLOCK_SOURCE X1000_CLK_SCLK_A
    {
        .msc_nr = 0,
        .msc_type = MSC_TYPE_SD,
        .bus_width = 4,
        .label = "microSD",
        .cd_gpio = GPIO_MSC0_CD,
        .cd_active_level = 0,
    },
#elif defined(SHANLING_Q1)
#define MSC_CLOCK_SOURCE  X1000_CLK_MPLL
    {
        .msc_nr = 0,
        .msc_type = MSC_TYPE_SD,
        .bus_width = 4,
        .label = "microSD",
        .cd_gpio = GPIO_MSC0_CD,
        .cd_active_level = 0,
    },
    /* NOTE: SDIO wifi card is on msc1 */
#elif defined(EROS_QN)
#define MSC_CLOCK_SOURCE X1000_CLK_SCLK_A
    {
        .msc_nr = 0,
        .msc_type = MSC_TYPE_SD,
        .bus_width = 4,
        .label = "microSD",
        .cd_gpio = GPIO_MSC0_CD,
        .cd_active_level = 0,
    },
#else
# error "Please add X1000 MSC config"
#endif
    {.msc_nr = -1},
};

void msc_soc_init_clock(int msc)
{
    /* Ensure correct clock source. MSC1 has no source field of its own. */
    (void)msc;
    jz_writef(CPM_MSC0CDR, CE(1), CLKDIV(0),
              CLKSRC(MSC_CLOCK_SOURCE == X1000_CLK_MPLL ? 1 : 0));
    while(jz_readf(CPM_MSC0CDR, BUSY));
    jz_writef(CPM_MSC0CDR, CE(0));
}

unsigned msc_soc_set_clock(int msc, unsigned rate)
{
    /* The /2 mirrors INCLK_SHIFT(1) on both MSC clocks. */
    uint32_t src_freq = clk_get(MSC_CLOCK_SOURCE) / 2;
    uint32_t div = clk_calc_div(src_freq, rate);
    if(msc == 0) {
        jz_writef(CPM_MSC0CDR, CE(1), CLKDIV(div - 1));
        while(jz_readf(CPM_MSC0CDR, BUSY));
        jz_writef(CPM_MSC0CDR, CE(0));
    } else {
        jz_writef(CPM_MSC1CDR, CE(1), CLKDIV(div - 1));
        while(jz_readf(CPM_MSC1CDR, BUSY));
        jz_writef(CPM_MSC1CDR, CE(0));
    }

    return src_freq / div;
}
