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

#include "audiohw.h"
#include "cs43131.h"
#include "system.h"
#include "kernel.h"
#include "pcm_sampr.h"
#include "generic_i2c.h"
#include "aic-x1600.h"
#include "clk-x1600.h"
#include "gpio-x1600.h"

/* Audio path: X1600 AIC (I2S) -> CS43131 -> 3.5 mm jack. The SoC is I2S master
 * and supplies MCLK; the DAC is a pure slave. Plain I2S, 64fs BCLK.
 *
 * Three X1600-vs-X1000 traps: the AIC base moved; there is no internal codec,
 * so no ICDC bit, no aic_set_external_codec() call and x1000-codec.c is dead;
 * and AICFR's master-mode controls are TMASTER/RMASTER where the X1000 had
 * BCKD/SYNCD with different semantics. The TX clock comes from EPLL through
 * the divider at CPM+0x7c, not the unused capture one at +0x60. */

/* Bit-banged I2C to the CS43131. Its pins are a second i2c0 pair, but the
 * controller is already committed to the AXP2101 and CW2015, so the bus is
 * driven in software at the vendor's 5 us per phase.
 *
 * SDA is push-pull rather than emulated open-drain: these are pull-down pads
 * with no internal pull, so "release and float high" would not work. The bus
 * relies on external pull-ups, and generic_i2c releases SDA to input before
 * every ACK and read, so the slave is never fought. */
#define CODEC_I2C_DELAY_US 5

static void codec_i2c_scl_dir(bool out)
{
    gpio_set_function(GPIO_CODEC_I2C_SCL,
                      out ? GPIOF_OUTPUT(1) : GPIOF_INPUT);
}

static void codec_i2c_sda_dir(bool out)
{
    gpio_set_function(GPIO_CODEC_I2C_SDA,
                      out ? GPIOF_OUTPUT(1) : GPIOF_INPUT);
}

static void codec_i2c_scl_out(bool high)
{
    gpio_set_level(GPIO_CODEC_I2C_SCL, high ? 1 : 0);
}

static void codec_i2c_sda_out(bool high)
{
    gpio_set_level(GPIO_CODEC_I2C_SDA, high ? 1 : 0);
}

static bool codec_i2c_scl_in(void)
{
    return gpio_get_level(GPIO_CODEC_I2C_SCL) ? true : false;
}

static bool codec_i2c_sda_in(void)
{
    return gpio_get_level(GPIO_CODEC_I2C_SDA) ? true : false;
}

static void codec_i2c_delay(int d)
{
    udelay(d);
}

static const struct i2c_interface codec_i2c_iface = {
    .scl_dir        = codec_i2c_scl_dir,
    .sda_dir        = codec_i2c_sda_dir,
    .scl_out        = codec_i2c_scl_out,
    .sda_out        = codec_i2c_sda_out,
    .scl_in         = codec_i2c_scl_in,
    .sda_in         = codec_i2c_sda_in,
    .delay          = codec_i2c_delay,

    .delay_hd_sta   = CODEC_I2C_DELAY_US,
    .delay_hd_dat   = CODEC_I2C_DELAY_US,
    .delay_su_dat   = CODEC_I2C_DELAY_US,
    .delay_su_sto   = CODEC_I2C_DELAY_US,
    .delay_su_sta   = CODEC_I2C_DELAY_US,
    .delay_thigh    = CODEC_I2C_DELAY_US,
};

/* audiohw interface */

static int cur_fsel = HW_FREQ_44;
static int cur_vol_l = 0, cur_vol_r = 0;

void audiohw_init(void)
{
    /* AIC: I2S, SoC is master for BCLK and LRCLK */
    aic_set_i2s_mode(AIC_I2S_MASTER_MODE);
    aic_enable_i2s_bit_clock(true);

    /* Bring up the software I2C bus and hand the DAC driver its index */
    int bus = i2c_add_node(&codec_i2c_iface);
    cs43131_init(bus);

    /* Seed the DAC driver's shadow before the part is opened: cs43131_open()
     * replays a vendor table whose shipped volume is full scale, and unmutes
     * on the way out */
    cs43131_set_volume(cur_vol_l, cur_vol_r);
    audiohw_set_frequency(cur_fsel);

    cs43131_open();
}

void audiohw_postinit(void)
{
    cs43131_mute(false);
}

void audiohw_close(void)
{
    cs43131_close();
}

void audiohw_set_frequency(int fsel)
{
    int sampr = hw_freq_sampr[fsel];

    /* Stop the bit clock while the divider is reprogrammed, or the DAC sees a
     * glitching BCLK. The third argument is the MCLK/fs ratio, not BCLK/fs --
     * passing 64 sounds plausible but starves the DAC of its master clock. */
    aic_enable_i2s_bit_clock(false);
    aic_set_i2s_clock(X1600_CLK_EPLL, sampr, AIC_I2S_MCLK_MULT);
    aic_enable_i2s_bit_clock(true);

    cs43131_set_frequency(sampr);

    cur_fsel = fsel;
}

void audiohw_set_volume(int vol_l, int vol_r)
{
    cur_vol_l = vol_l;
    cur_vol_r = vol_r;
    cs43131_set_volume(vol_l, vol_r);
}

/* Target-local, not a core hook. PWDN_CTL is the only mute mechanism there is
 * evidence for on the CS43131, verified by writing it mid-stream. */
void audiohw_mute(bool mute)
{
    cs43131_mute(mute);
}

/* CS43131 target hooks */

void cs43131_set_power_pin(int level)
{
    /* PB02, active high (vendor cs43131_pwr_en_level = 1) */
    gpio_set_level(GPIO_CS43131_POWER, level ? 1 : 0);
}

void cs43131_set_reset_pin(int level)
{
    /* PB21, active high (vendor cs43131_rst_en_level = 1) */
    gpio_set_level(GPIO_CS43131_RESET, level ? 1 : 0);
}

/* No anti-pop pre-roll: the vendor's 120 ms dummy-data feature is compiled in
 * but disabled on this unit. If the hardware pops on the first note, feed
 * silence through the AIC before releasing PWDN_CTL. */
