/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
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

#ifndef __CS43131_H__
#define __CS43131_H__

#include <stdbool.h>
#include <stdint.h>

/* Cirrus Logic CS43131 headphone DAC/amp.
 *
 * ============================ WARNING ==============================
 *
 * THIS PART HAS FOUR VOLUME REGISTERS AND 0x00 MEANS 0 dB, i.e. FULL
 * SCALE. There is no separate mute latch, no strobe register, and no
 * "apply" bit: the moment a volume register is written, that gain is
 * live.
 *
 * The vendor driver keeps a 24-entry register table which it replays
 * verbatim on *every* stream start, and that table ships with all four
 * volume registers set to 0x00. A driver that copies the vendor table
 * literally will therefore slam the output to full scale on every
 * single play/pause cycle, into headphones, with no ramp.
 *
 * The rule for this driver is: seed the four volume entries in the
 * shadow table from the driver's current volume BEFORE walking the
 * table. cs43131_open() does exactly that. Do not reorder it, and do
 * not "optimise" the seeding out because "audiohw_set_volume() is
 * called right afterwards anyway" - the table walk unmutes the part
 * (PWDN_CTL 0xBE -> 0xAE) as its second-to-last step, so anything
 * after the walk is already too late by several milliseconds of
 * full-scale output.
 *
 * ===================================================================
 *
 * Control interface:
 *   - I2C slave address 0x30 (7-bit).
 *   - Registers are 24-bit addresses. A write is a single 5-byte I2C
 *     payload: { reg>>16, reg>>8, reg, 0x00, value }.
 *   - Reads always return 0x00 on this board, so the part is treated
 *     as write-only with a driver-side shadow.
 *   - On the HiBy R1 the bus is bit-banged on PB30/PB31; the hardware
 *     i2c0 controller is busy with the PMU on a different pin pair.
 */

/* 7-bit I2C address. generic_i2c wants the 8-bit form, so callers pass
 * (CS43131_I2C_ADDR << 1). */
#define CS43131_I2C_ADDR        0x30

/* ---- Registers we understand -------------------------------------- */

/* PWDN_CTL. Writing 0xBE mid-stream silences the output and 0xAE restores
 * it. Used as the mute control because
 * the vendor driver's own digital_mute callback is a no-op stub. */
#define CS43131_REG_PWDN_CTL    0x020000
#define CS43131_PWDN_DOWN       0xbe
#define CS43131_PWDN_RELEASE    0xae

/* Serial port sample rate selector. Verified from the vendor's
 * cs43131_dai_prepare(): it is the only register written per-rate on
 * the PCM path, with the code table reproduced in cs43131.c. */
#define CS43131_REG_SP_SRATE    0x01000b
#define CS43131_REG_SP_SRATE2   0x01000c    /* 0x00 for PCM, 0x05 for DSD */

/* PCM/DSD data path select. 0x02 for PCM; for DoP the vendor writes
 * 0x50 | (n << 2). */
#define CS43131_REG_PATH_SEL    0x070004
#define CS43131_PATH_PCM        0x02

/* ---------------- THE FOUR VOLUME REGISTERS ------------------------
 * 0x00 = 0 dB = FULL SCALE, 0xFF = -127.5 dB, 0.5 dB per step.
 * A and B are left and right respectively; verified from the vendor's
 * cs43131_vol_left_put() (writes 0x090002 then 0x070001) and
 * cs43131_vol_right_put() (writes 0x090001 then 0x070000).
 */
#define CS43131_REG_PCM_VOL_A   0x090002    /* left  */
#define CS43131_REG_PCM_VOL_B   0x090001    /* right */
#define CS43131_REG_DSD_VOL_A   0x070001    /* left  */
#define CS43131_REG_DSD_VOL_B   0x070000    /* right */

/* Attenuation register value used until the driver is told otherwise.
 * 0x50 = 80 steps * 0.5 dB = -40 dB. Chosen so that a bug elsewhere
 * cannot produce a full-scale surprise. */
#define CS43131_VOL_SAFE_DEFAULT 0x50

/* ---- Rockbox volume scale ------------------------------------------
 * Hardware resolution is 0.5 dB, so the setting is carried in tenths
 * of a dB with a step of 5, exactly like the ES9218 driver does.
 * 0xFF is the largest attenuation the register can express. */
#define CS43131_VOLUME_MIN      (-1275)
#define CS43131_VOLUME_MAX      0
#define CS43131_VOLUME_STEP     5

#define AUDIOHW_CAPS            0

AUDIOHW_SETTING(VOLUME, "dB", 1, CS43131_VOLUME_STEP,
                CS43131_VOLUME_MIN, CS43131_VOLUME_MAX, -400)

/* ---- Driver API ---------------------------------------------------- */

/* One-time setup. 'i2c_bus' is the generic_i2c bus index returned by
 * i2c_add_node() for the bit-banged codec bus. Leaves the part powered
 * down; also seeds the volume shadow to CS43131_VOL_SAFE_DEFAULT. */
extern void cs43131_init(int i2c_bus);

/* Power the part up and program it. Applies the current volume, then
 * replays the vendor register table, then applies the sample rate. */
extern void cs43131_open(void);

/* Mute and power the part down, dropping the reset and power pins. */
extern void cs43131_close(void);

/* Mute/unmute via PWDN_CTL. Safe to call while closed (no-op). */
extern void cs43131_mute(bool mute);

/* Set volume in tenths of a dB, range CS43131_VOLUME_MIN..MAX.
 * Always updates the shadow, so a later cs43131_open() replays it. */
extern void cs43131_set_volume(int vol_l, int vol_r);

/* Select the serial-port sample rate. 'sampr' is in Hz. */
extern void cs43131_set_frequency(int sampr);

/* Read back the driver's shadow copy of a register; returns -1 if the
 * register is not one the shadow tracks. Debug aid only - the part
 * itself always reads back 0x00. */
extern int cs43131_read_shadow(uint32_t reg);

/* Control-bus health since boot: writes attempted, writes the part did not
 * ACK, and the first error (-2 = no ACK on the slave address, i.e. the DAC is
 * not answering at all). Any pointer may be NULL.
 *
 * This is the only evidence that the part is really there. It reads back 0x00
 * for every register, so a bus that never reaches it looks exactly like one
 * that works and the symptom is silence with nothing logged anywhere. */
extern void cs43131_bus_status(unsigned* writes, unsigned* naks, int* first_err);

/* ---- Hooks the target must provide -------------------------------- */

/* Drive the DAC's power and reset pins. Both are active high on the
 * HiBy R1 (PB02 and PB21). */
extern void cs43131_set_power_pin(int level);
extern void cs43131_set_reset_pin(int level);

#endif /* __CS43131_H__ */
