/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
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

/* Board and SoC glue for the shared MSC driver (ingenic_common/msc-ingenic.c).
 *
 * Two things are genuinely this part's rather than the X1000's:
 *
 *  1. The microSD is on MSC1 (PD00-PD05); MSC0 is the SDIO wifi. Every X1000
 *     Rockbox target puts the card on MSC0 -- do not copy them.
 *  2. Each CDR has its own 2-bit MPCS source mux, where the X1000's MSC1
 *     borrows MSC0CDR.CLKSRC, and the field names differ throughout.
 */

#include "system.h"
#include "panic.h"
#include "ingenic-soc.h"

const msc_config msc_configs[] = {
#if defined(HIBY_R1_NATIVE)
/* Clock source. MPLL at 1400 MHz gives 700 MHz after its fixed /2, which
 * divides exactly to both 25 and 50 MHz. SCLK_A would work arithmetically but
 * is also subject to CPCCR.GATE_SCLKA; EPLL is the audio/display parent.
 * TODO(x1600): unverified -- the vendor's own MSC1 parent was never captured.
 * ⚠ Reading CPM_MSC1CDR back does NOT answer this:
 * it read 0x4000000d consistently, which is only what msc_soc_set_clock() had
 * just written. The register reports our choice, not the vendor's. Answering it
 * needs a capture from the stock firmware, or a read before our driver runs.
 * msc_soc_set_clock() panics rather than dividing by zero if it is stopped. */
#define MSC_CLOCK_SOURCE  X1600_CLK_MPLL
#define MSC_CLOCK_MPCS    BV_CPM_MSC1CDR_MPCS__MPLL
    {
        /* !! MSC *1* !!  From /sys/kernel/debug/gpio labels
         * (msc1_clk, msc1_cmd, msc1_d0..d3) and PM Table 21-5 (line 19662). */
        .msc_nr = 1,
        .msc_type = MSC_TYPE_SD,
        .bus_width = 4,
        .label = "microSD",
        .cd_gpio = GPIO_MSC1_CD,        /* PB22 */
        .cd_active_level = 0,           /* vendor DT: cd-inverted, level 0 */
        .pwr_gpio = GPIO_MSC1_POWER,    /* PC25 */
        .pwr_active_level = 1,          /* vendor DT: msc1_pwr_enable_level=1 */
        .pin_port = GPIO_D,
        .pin_mask = 0x3f << 0,          /* PD00..PD05 */
        .pin_func = GPIOF_DEVICE(0),    /* PM Table 21-5 Fun_0 = msc1_* */
    },
    /* NOTE: MSC0 is the SDIO wifi module (PB12-PB17) and is deliberately NOT
     * listed.  Rockbox has no use for it, its CLKGR gate is left closed by
     * system_init(), and its INTC source is never unmasked. */
#else
# error "Please add X1600 MSC config"
#endif
    {.msc_nr = -1},
};

/* Ratio = (reg + 1) * 2, so the field holds ratio/2 - 1 and the representable
 * ratios are the even numbers 2..512 (PM 11.1.2.9). The write-with-CE,
 * poll-BUSY, clear-CE sequence is the X1000's. */
static void msc_set_cdr(int msc, uint32_t mpcs, uint32_t ratio)
{
    if(ratio < 2)
        ratio = 2;
    else if(ratio > 512)
        ratio = 512;

    uint32_t regdiv = (ratio / 2) - 1;

    if(msc == 0) {
        jz_writef(CPM_MSC0CDR, CE_MSC(1), MPCS(mpcs), MSCCDR(regdiv));
        (void)x1600_spin_while(jz_readf(CPM_MSC0CDR, MSC_BUSY));
        jz_writef(CPM_MSC0CDR, CE_MSC(0));
    } else {
        jz_writef(CPM_MSC1CDR, CE_MSC(1), MPCS(mpcs), MSCCDR(regdiv));
        (void)x1600_spin_while(jz_readf(CPM_MSC1CDR, MSC_BUSY));
        jz_writef(CPM_MSC1CDR, CE_MSC(0));
    }
}

void msc_soc_init_clock(int msc)
{
    /* msc_set_speed() reprograms this a few instructions later, so the ratio
     * only has to survive that gap. The X1000 writes the smallest ratio here,
     * which would briefly feed the MSC 700 MHz; the largest costs nothing and
     * keeps the transient at 2.7 MHz. */
    msc_set_cdr(msc, MSC_CLOCK_MPCS, 512);
}

unsigned msc_soc_set_clock(int msc, unsigned rate)
{
    /* The /2 mirrors the register's fixed extra halving (PM 11.1.2.9); the
     * X1000 tags both MSC clocks INCLK_SHIFT(1) for the same effect. */
    uint32_t src_freq = clk_get(MSC_CLOCK_SOURCE) / 2;

    /* clk_init() leaves MPLL and EPLL alone, so entering Rockbox without a
     * bootloader having started the chosen PLL gives clk_get() == 0. Dividing
     * by that would turn a missing clock into an unexplained lockup. */
    if(UNLIKELY(src_freq == 0)) {
        panicf("msc%d: clock source %s is stopped", msc,
               clk_get_name(MSC_CLOCK_SOURCE));
    }

    uint32_t div = clk_calc_div(src_freq, rate);
    msc_set_cdr(msc, MSC_CLOCK_MPCS, 2 * div);
    return src_freq / div;
}
