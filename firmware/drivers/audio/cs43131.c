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

/* Control paths only -- open/close/volume, never a per-sample or per-buffer
 * path. On this target the console rides the USB data path, so anything
 * logged in proportion to throughput consumes the throughput it measures. */
#define LOGF_ENABLE

#include "audiohw.h"
#include "cs43131.h"
#include "generic_i2c.h"
#include "system.h"
#include "kernel.h"
#include "logf.h"

/* Cirrus Logic CS43131 driver.
 *
 * We have no CS43131 datasheet. Everything here is either measured on
 * hardware or reverse-engineered from the vendor kernel module
 * codec_cs43131.ko (MIPS disassembly of cs43131_dai_prepare,
 * cs43131_dai_startup, cs43131_dai_shutdown, cs43131_i2c_write_reg,
 * cs43131_vol_left_put and cs43131_vol_right_put, plus a hexdump of the
 * module's .data section where the register table lives).
 *
 * Consequently: most register *names* below are unknown and the raw
 * 24-bit addresses are used. Please do not invent names for them.
 *
 * READ THE WARNING AT THE TOP OF firmware/export/cs43131.h BEFORE
 * TOUCHING THE ORDER OF OPERATIONS IN cs43131_open().
 */

struct cs43131_reg {
    uint32_t reg;
    uint8_t  val;
};

/* The vendor's initialisation table, byte-for-byte.
 *
 * Source: codec_cs43131.ko, symbol cs43131_reg, .data offset 0x03b8,
 * size 0xc8. Each entry is a little-endian u32 register address
 * followed by a little-endian u32 value; the list is terminated by
 * register 0x00ffffff. 24 real entries + terminator = 200 bytes, which
 * matches the symbol size exactly.
 *
 * cs43131_dai_prepare() walks this table and writes every entry, in
 * order, on every stream start. Volume writes (cs43131_vol_*_put)
 * patch the matching table entries in place, which is how the vendor
 * driver keeps volume across a stop/start - but the *shipped* values
 * for the four volume registers are 0x00, so the very first stream
 * after boot plays at full scale on the OF too.
 *
 * The two 0x020000 entries near the end are PWDN_CTL: the table powers
 * the part down (0xBE) and then releases it (0xAE). Everything before
 * that point is therefore programmed while muted, which is why seeding
 * the volumes before the walk is sufficient - and why doing it after
 * the walk is not.
 */
static struct cs43131_reg cs43131_regs[] = {
    { 0x010006, 0x04 },
    { 0x020052, 0x06 },
    { 0x01000b, 0x01 },     /* SP_SRATE, overwritten per-rate below */
    { 0x01000c, 0x00 },
    { 0x05000a, 0x07 },
    { 0x05000b, 0x0f },
    { 0x06000a, 0x07 },
    { 0x06000b, 0x0f },
    { 0x070000, 0x00 },     /* !! DSD_VOL_B (right) - 0x00 = FULL SCALE */
    { 0x070001, 0x00 },     /* !! DSD_VOL_A (left)  - 0x00 = FULL SCALE */
    { 0x070004, 0x02 },     /* PATH_SEL: PCM */
    { 0x070006, 0x40 },
    { 0x090000, 0x02 },
    { 0x090001, 0x00 },     /* !! PCM_VOL_B (right) - 0x00 = FULL SCALE */
    { 0x090002, 0x00 },     /* !! PCM_VOL_A (left)  - 0x00 = FULL SCALE */
    { 0x090003, 0x90 },
    { 0x090004, 0x00 },
    { 0x0b0000, 0x06 },
    { 0x080000, 0x31 },
    { 0x070002, 0xa0 },
    { 0x020000, 0xbe },     /* PWDN_CTL: power down */
    { 0x020000, 0xae },     /* PWDN_CTL: release */
    { 0x09000a, 0xf0 },
    { 0x09000b, 0x0c },
};

/* Serial-port sample-rate codes.
 *
 * Verified from cs43131_dai_prepare()'s PCM branch, which is a chain of
 * compares against these exact rates; anything that does not match
 * falls through to code 1 (the 44.1 kHz code).
 */
static const struct {
    uint32_t rate;
    uint8_t  code;
} cs43131_srate_tbl[] = {
    {  32000, 0 },
    {  44100, 1 },
    {  48000, 2 },
    {  88200, 3 },
    {  96000, 4 },
    { 176400, 5 },
    { 192000, 6 },
    { 352800, 7 },
    { 384000, 8 },
};

#define CS43131_SRATE_FALLBACK 1

static int cs_bus = -1;
static bool cs_open = false;
static bool cs_muted = true;

/* Current volume in tenths of a dB, and the register codes derived
 * from it. Initialised to the safe attenuation so that even a
 * cs43131_open() with no preceding cs43131_set_volume() cannot produce
 * full scale. */
static int cs_vol_l = -(CS43131_VOL_SAFE_DEFAULT * CS43131_VOLUME_STEP);
static int cs_vol_r = -(CS43131_VOL_SAFE_DEFAULT * CS43131_VOLUME_STEP);
static uint8_t cs_volreg_l = CS43131_VOL_SAFE_DEFAULT;
static uint8_t cs_volreg_r = CS43131_VOL_SAFE_DEFAULT;

static int cs_sampr = 44100;

/* ---- transport ------------------------------------------------------ */

/* One register write is a single 5-byte I2C payload:
 *   { reg>>16, reg>>8, reg, 0x00, value }
 * Verified from cs43131_i2c_write_reg(), which builds exactly this on
 * the stack (buf[3] is written as zero) and hands it to i2c_transfer()
 * with len = 5. */
/* Bus health. The part reads back 0x00 for everything, so a write that never
 * reached it is otherwise indistinguishable from one that did -- the failure
 * is silence with no error anywhere. The address ACK is the one piece of
 * evidence the bus does give us, so count it. */
static unsigned cs_writes;
static unsigned cs_naks;
static int cs_first_err;

static int cs43131_write_hw(uint32_t reg, uint8_t val)
{
    uint8_t buf[5];

    if(cs_bus < 0)
        return -1;

    buf[0] = (reg >> 16) & 0xff;
    buf[1] = (reg >> 8) & 0xff;
    buf[2] = reg & 0xff;
    buf[3] = 0x00;
    buf[4] = val;

    /* No register-address byte of our own: the whole 5-byte block is
     * the payload, hence -1 for generic_i2c's 'address' argument. */
    int rc = i2c_write_data(cs_bus, CS43131_I2C_ADDR << 1, -1, buf, sizeof(buf));

    cs_writes++;
    if(rc < 0) {
        cs_naks++;
        if(cs_first_err == 0)
            cs_first_err = rc;
    }

    return rc;
}

void cs43131_bus_status(unsigned* writes, unsigned* naks, int* first_err)
{
    if(writes)    *writes    = cs_writes;
    if(naks)      *naks      = cs_naks;
    if(first_err) *first_err = cs_first_err;
}

/* Update the shadow entry for 'reg', if the table has one. Returns
 * true if at least one entry was patched. */
static bool cs43131_shadow_poke(uint32_t reg, uint8_t val)
{
    bool found = false;

    for(unsigned i = 0; i < ARRAYLEN(cs43131_regs); ++i) {
        if(cs43131_regs[i].reg == reg) {
            /* PWDN_CTL appears twice on purpose (down then release);
             * never let a shadow update rewrite those. */
            if(reg == CS43131_REG_PWDN_CTL)
                continue;

            cs43131_regs[i].val = val;
            found = true;
        }
    }

    return found;
}

/* Write a register and keep the shadow in sync. */
static void cs43131_write(uint32_t reg, uint8_t val)
{
    cs43131_shadow_poke(reg, val);
    cs43131_write_hw(reg, val);
}

/* ---- volume -------------------------------------------------------- */

/* Convert tenths of a dB of attenuation to a register code.
 * 0 dB -> 0x00 (full scale), -127.5 dB -> 0xFF. */
static uint8_t cs43131_vol_to_reg(int vol)
{
    if(vol > CS43131_VOLUME_MAX)
        vol = CS43131_VOLUME_MAX;
    if(vol < CS43131_VOLUME_MIN)
        vol = CS43131_VOLUME_MIN;

    int steps = -vol / CS43131_VOLUME_STEP;
    if(steps < 0)
        steps = 0;
    if(steps > 0xff)
        steps = 0xff;

    return (uint8_t)steps;
}

/* Push the current volume into the four shadow entries.
 *
 * *** THIS MUST RUN BEFORE THE TABLE WALK IN cs43131_open(). ***
 *
 * The four registers are the entire reason this driver has a shadow
 * table at all. See the warning in cs43131.h.
 */
static void cs43131_seed_volume_shadow(void)
{
    cs43131_shadow_poke(CS43131_REG_PCM_VOL_A, cs_volreg_l);
    cs43131_shadow_poke(CS43131_REG_PCM_VOL_B, cs_volreg_r);
    cs43131_shadow_poke(CS43131_REG_DSD_VOL_A, cs_volreg_l);
    cs43131_shadow_poke(CS43131_REG_DSD_VOL_B, cs_volreg_r);
}

void cs43131_set_volume(int vol_l, int vol_r)
{
    cs_vol_l = vol_l;
    cs_vol_r = vol_r;
    cs_volreg_l = cs43131_vol_to_reg(vol_l);
    cs_volreg_r = cs43131_vol_to_reg(vol_r);

    cs43131_seed_volume_shadow();

    if(!cs_open)
        return;

    /* Same order the vendor uses: PCM first, then DSD. */
    cs43131_write_hw(CS43131_REG_PCM_VOL_A, cs_volreg_l);
    cs43131_write_hw(CS43131_REG_DSD_VOL_A, cs_volreg_l);
    cs43131_write_hw(CS43131_REG_PCM_VOL_B, cs_volreg_r);
    cs43131_write_hw(CS43131_REG_DSD_VOL_B, cs_volreg_r);
}

/* ---- rate ---------------------------------------------------------- */

static uint8_t cs43131_srate_code(int sampr)
{
    for(unsigned i = 0; i < ARRAYLEN(cs43131_srate_tbl); ++i)
        if(cs43131_srate_tbl[i].rate == (uint32_t)sampr)
            return cs43131_srate_tbl[i].code;

    return CS43131_SRATE_FALLBACK;
}

void cs43131_set_frequency(int sampr)
{
    cs_sampr = sampr;

    uint8_t code = cs43131_srate_code(sampr);

    /* PCM path only. DoP/DSD support would additionally write
     * PATH_SEL = 0x50 | (n << 2) and SP_SRATE2 = 0x05; the vendor's
     * mapping there is n = 1 for DSD64 (176.4 kHz carrier) and n = 2
     * for DSD128 (352.8 kHz carrier). Rockbox has no DSD path, so this
     * driver stays on PCM. */
    cs43131_write(CS43131_REG_PATH_SEL, CS43131_PATH_PCM);
    cs43131_write(CS43131_REG_SP_SRATE, code);
}

/* ---- power / open / close ------------------------------------------ */

void cs43131_init(int i2c_bus)
{
    cs_bus = i2c_bus;
    cs_open = false;
    cs_muted = true;

    /* Belt and braces: even before anyone calls set_volume(), make the
     * table safe to replay. */
    cs43131_seed_volume_shadow();

    /* Leave the part unpowered until a stream starts, matching the
     * vendor driver (its power/reset pins are driven from
     * dai_startup/dai_shutdown, not from probe). */
    cs43131_set_reset_pin(0);
    cs43131_set_power_pin(0);
}

void cs43131_open(void)
{
    if(cs_open)
        return;

    /* Power-up sequence, transcribed from cs43131_dai_startup():
     * power pin high, 10 x udelay(1000), reset pin high, another
     * 10 x udelay(1000). Both pins are active high on this board
     * (vendor cs43131_pwr_en_level=1, cs43131_rst_en_level=1). */
    cs43131_set_power_pin(1);
    mdelay(10);
    cs43131_set_reset_pin(1);
    mdelay(10);

    cs_open = true;

    /* *** ORDER IS SAFETY-CRITICAL ***
     *
     * Seed the four volume registers in the shadow FIRST. The table
     * walk below writes those shadow values, and then releases
     * PWDN_CTL as its 22nd entry. If the shadow still held the
     * vendor's 0x00 defaults at this point, the part would come out of
     * power-down at 0 dB - full scale - straight into headphones.
     */
    cs43131_seed_volume_shadow();

    for(unsigned i = 0; i < ARRAYLEN(cs43131_regs); ++i)
        cs43131_write_hw(cs43131_regs[i].reg, cs43131_regs[i].val);

    /* The table left the part released (PWDN_CTL = 0xAE). */
    cs_muted = false;

    /* Rate-dependent writes, which the vendor also does after the
     * table walk. */
    cs43131_set_frequency(cs_sampr);

    /* Say whether the part actually answered. Silence with naks == writes means
     * the control bus never reached it and nothing above took effect; the DAC
     * reads back 0x00 for everything, so this is the only evidence there is. */
    logf("cs43131: open, i2c %u writes %u nak (err %d)",
         cs_writes, cs_naks, cs_first_err);
}

void cs43131_close(void)
{
    if(!cs_open) {
        cs43131_set_reset_pin(0);
        cs43131_set_power_pin(0);
        return;
    }

    /* Mute before cutting the rails so we do not click. */
    cs43131_mute(true);
    mdelay(2);

    /* Vendor cs43131_dai_shutdown() drops the power pin first, then
     * reset. */
    cs43131_set_power_pin(0);
    cs43131_set_reset_pin(0);

    cs_open = false;
}

void cs43131_mute(bool mute)
{
    if(!cs_open)
        return;

    if(mute == cs_muted)
        return;

    /* 0xBE silences a live stream and 0xAE restores it. The vendor's own
     * digital_mute / soft_mute / mute_put
     * callbacks are all empty stubs, so PWDN_CTL is the only mute
     * mechanism we have evidence for. */
    cs43131_write_hw(CS43131_REG_PWDN_CTL,
                     mute ? CS43131_PWDN_DOWN : CS43131_PWDN_RELEASE);
    cs_muted = mute;
}

int cs43131_read_shadow(uint32_t reg)
{
    for(unsigned i = 0; i < ARRAYLEN(cs43131_regs); ++i)
        if(cs43131_regs[i].reg == reg)
            return cs43131_regs[i].val;

    return -1;
}
