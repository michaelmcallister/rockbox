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
 * Based on firmware/target/mips/ingenic_x1000/aic-x1000.h,
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

#ifndef __AIC_X1600_H__
#define __AIC_X1600_H__

#include "clk-x1600.h"
#include "x1600/aic.h"
#include "x1600/cpm.h"
#include <stdbool.h>
#include <stdint.h>

/* Do not reuse aic-x1000.h. Five silent-failure traps: the base moved;
 * AICFR's low bits are a different register; I2SCR has lost ESCLK and STPBK, so
 * there is no in-AIC bit-clock stop; there is no internal codec, making
 * x1000-codec.c dead; and the bit-clock divider is not "reg + 1". */

/* Arguments to aic_set_i2s_mode(). Master mode splits into TMASTER and
 * RMASTER here; share-clock mode makes TMASTER the one that matters. */
#define AIC_I2S_MASTER_MODE 0
#define AIC_I2S_SLAVE_MODE  1

/* Arguments to aic_set_i2s_channel_order() */
#define AIC_I2S_LEFT_CHANNEL_FIRST  0
#define AIC_I2S_RIGHT_CHANNEL_FIRST 1

/* MCLK reads back at 512 fs, I2SDIV = 8 giving a 64 fs BCLK */
#define AIC_I2S_MCLK_MULT 512
#define AIC_I2S_BCLK_MULT 64

/* Meant to be called with immediate constants, so each collapses to one
 * register read-modify-write */

/* Send the last sample rather than a zero one on a TX under-run. Rockbox
 * wants false: repeating gives a held DC level. */
static inline void aic_set_play_last_sample(bool en)
{
    jz_writef(AIC_FR, LSMP(en ? 1 : 0));
}

/* Master/slave selection. See the comment on AIC_I2S_MASTER_MODE. */
static inline void aic_set_i2s_mode(int mode)
{
    switch(mode) {
    default:
    case AIC_I2S_MASTER_MODE:
        jz_writef(AIC_FR, DMODE(0), TMASTER(1), RMASTER(1));
        break;

    case AIC_I2S_SLAVE_MODE:
        jz_writef(AIC_FR, DMODE(0), TMASTER(0), RMASTER(0));
        break;
    }
}

/* Channel order on the wire, playback only; set it before replay starts */
static inline void aic_set_i2s_channel_order(int order)
{
    switch(order) {
    default:
    case AIC_I2S_LEFT_CHANNEL_FIRST:
        jz_writef(AIC_I2SCR, RFIRST(0));
        break;

    case AIC_I2S_RIGHT_CHANNEL_FIRST:
        jz_writef(AIC_I2SCR, RFIRST(1));
        break;
    }
}

/* Enable/disable the I2S bit clock. There is no I2SCR.STPBK here, so CE_I2S
 * is the only lever upstream of BCLK. UNVERIFIED: if it is a "change enable"
 * rather than a running-clock one, disabling does nothing and a rate change
 * glitches BCLK -- a click, not a hazard. */
static inline void aic_enable_i2s_bit_clock(bool en)
{
    jz_writef(CPM_I2S1CDR, CE_I2S(en ? 1 : 0));
}

static inline bool aic_i2s_bit_clock_enabled(void)
{
    return jz_readf(CPM_I2S1CDR, CE_I2S) != 0;
}

/* Program the I2S clocks for a sample rate.  'clksrc' must be EPLL, the only
 * alternative being SCLK_A, which is shared with the DDR/AHB tree and far too
 * jittery for a DAC.  'mult' is the MCLK/fs ratio: a multiple of 64, with
 * mult/64 even. Returns 0, or nonzero without touching a register.
 *
 * The PM contradicts itself on the bit-clock divider; hardware settles it,
 * I2SDIV reading back as 8 while the vendor played 44.1 kHz at 512 fs. Write
 * TDIV = mult/64 -- aic-x1000.c's "i2sdiv - 1" must not be copied. */
extern int aic_set_i2s_clock(x1600_clk_t clksrc, uint32_t fs, uint32_t mult);

#endif /* __AIC_X1600_H__ */
