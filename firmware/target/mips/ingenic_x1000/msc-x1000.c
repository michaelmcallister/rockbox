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
#include "sdmmc_host.h"
#include "msc-ingenic.h"
#include "gpio-ingenic.h"
#include "clk-x1000.h"
#include "x1000/cpm.h"

#if defined(FIIO_M3K)
# define MSC_CLOCK_SOURCE X1000_CLK_SCLK_A
#elif defined(SHANLING_Q1)
# define MSC_CLOCK_SOURCE X1000_CLK_MPLL
/* NOTE: SDIO wifi card is on msc1 */
#elif defined(EROS_QN)
# define MSC_CLOCK_SOURCE X1000_CLK_SCLK_A
#else
# error "Please add X1000 MSC config"
#endif

/* All three X1000 players wire the microSD slot to MSC0 the same way, and
 * only the clock source above differs between them.  The voltage range is
 * the one the driver used to ask for in the ACMD41 argument.
 */
const msc_config msc_configs[] = {
    {
        .msc_nr          = 0,
        .storage_type    = STORAGE_SD,
        .bus_voltages    = SDMMC_BUS_VOLTAGE_3V2_3V3 |
                           SDMMC_BUS_VOLTAGE_3V3_3V4,
        .bus_widths      = SDMMC_BUS_WIDTH_1BIT |
                           SDMMC_BUS_WIDTH_4BIT,
        .bus_clocks      = SDMMC_BUS_CLOCK_400KHZ |
                           SDMMC_BUS_CLOCK_25MHZ |
                           SDMMC_BUS_CLOCK_50MHZ,
        .cd_gpio         = GPIO_MSC0_CD,
        .cd_active_level = 0,
    },
    {.msc_nr = -1},
};

void msc_soc_init_clock(int msc)
{
    /* Both controllers take their parent from MSC0CDR.CLKSRC; MSC1CDR has
     * no source field of its own on this SOC. */
    (void)msc;

    jz_writef(CPM_MSC0CDR, CE(1), CLKDIV(0),
              CLKSRC(MSC_CLOCK_SOURCE == X1000_CLK_MPLL ? 1 : 0));
    while(jz_readf(CPM_MSC0CDR, BUSY));
    jz_writef(CPM_MSC0CDR, CE(0));
}

unsigned msc_soc_set_clock(int msc, unsigned rate)
{
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
