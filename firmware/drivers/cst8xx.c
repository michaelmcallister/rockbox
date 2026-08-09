/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Firmware   |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Jukebox    |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
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

/* CST8xx capacitive touch controller.
 *
 * The protocol is one 7-byte block read from register 0x00, verified against
 * the vendor driver's own output:
 *
 *   reg[0]      unused
 *   reg[1]      unused
 *   reg[2]      finger count
 *   reg[3]      bits 7:6 event  (00 = DOWN, 01 = UP, 10 = CONTACT)
 *               bits 3:0 X[11:8]
 *   reg[4]      X[7:0]
 *   reg[5]      bits 3:0 Y[11:8]
 *   reg[6]      Y[7:0]
 *
 * An idle block reads back as 00 00 00 ff ff ff ff. The block encodes exactly
 * one point, so CST8XX_NUM_POINTS is 1 and a count above it is by definition a
 * bad read rather than multitouch.
 *
 * ⚠ TORN READS decode to a plausible "count = 15, x = 4095, y = 4095", which
 * reaches the UI as a tap in the corner. Two defences, and BOTH are required:
 * the read is driven from the controller's interrupt rather than polled, and
 * cst8xx_valid() rejects any block that cannot be real. Neither is optional.
 *
 * The reset line, the rail and the interrupt pin belong to the board, not here
 * -- see button-hibyr1.c. Unlike ft6x06.c there is no event callback, because
 * the one board using this part reads the state directly; it is a few lines to
 * add if a second one needs it.
 */

#include "cst8xx.h"
#include "kernel.h"
#include "i2c-async.h"
#include <string.h>

#define CST8XX_BLOCK_LEN 7

struct cst8xx_driver {
    /* i2c bus data */
    int i2c_cookie;
    i2c_descriptor i2c_desc;

    /* buffer for I2C transfers: [0] is the register address written,
     * [1..7] the block read back */
    uint8_t raw_data[1 + CST8XX_BLOCK_LEN];
};

static struct cst8xx_driver cst_drv;
struct cst8xx_state cst8xx_state;

static void cst8xx_park(void)
{
    cst8xx_state.nr_points = 0;
    for(int i = 0; i < CST8XX_NUM_POINTS; ++i)
        cst8xx_state.points[i].event = CST8XX_EVT_UP;
}

static bool cst8xx_valid(const uint8_t* reg, int count, int x, int y)
{
    /* count > 1 catches the all-0xff torn read (which decodes to 15) as well
     * as any genuine multitouch report the panel should not be producing.
     * x/y bounds catch 4095/4095. */
    if(count > CST8XX_NUM_POINTS)
        return false;
    if(x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return false;

    /* Belt and braces: a completely blank block is the documented idle state,
     * not a touch. */
    if(reg[3] == 0xff && reg[4] == 0xff && reg[5] == 0xff && reg[6] == 0xff)
        return false;

    return true;
}

static void cst8xx_i2c_callback(int status, i2c_descriptor* desc)
{
    (void)desc;

    if(status != I2C_STATUS_OK)
        return;

    const uint8_t* reg = &cst_drv.raw_data[1];

    int count = reg[2];
    int x     = ((reg[3] & 0x0f) << 8) | reg[4];
    int y     = ((reg[5] & 0x0f) << 8) | reg[6];
    int event = (reg[3] >> 6) & 0x3;

    if(count == 0) {
        /* Explicit "no fingers" - report a release. */
        cst8xx_park();
        return;
    }

    if(!cst8xx_valid(reg, count, x, y))
        return;         /* torn read: keep the previous state */

    cst8xx_state.nr_points       = count;
    cst8xx_state.points[0].event = event;
    cst8xx_state.points[0].pos_x = x;
    cst8xx_state.points[0].pos_y = y;
}

void cst8xx_init(void)
{
    /* Initialize stuff */
    memset(&cst_drv, 0, sizeof(cst_drv));

    memset(&cst8xx_state, 0, sizeof(struct cst8xx_state));
    cst8xx_park();

    /* Reserve bus management cookie */
    cst_drv.i2c_cookie = i2c_async_reserve_cookies(CST8XX_BUS, 1);

    /* Prep an I2C descriptor to read touch data */
    cst_drv.i2c_desc.slave_addr = CST8XX_ADDR;
    cst_drv.i2c_desc.bus_cond   = I2C_START | I2C_STOP;
    cst_drv.i2c_desc.tran_mode  = I2C_READ;
    cst_drv.i2c_desc.buffer[0]  = &cst_drv.raw_data[0];
    cst_drv.i2c_desc.count[0]   = 1;
    cst_drv.i2c_desc.buffer[1]  = &cst_drv.raw_data[1];
    cst_drv.i2c_desc.count[1]   = CST8XX_BLOCK_LEN;
    cst_drv.i2c_desc.callback   = cst8xx_i2c_callback;
    cst_drv.i2c_desc.arg        = 0;
    cst_drv.i2c_desc.next       = NULL;

    /* Set I2C register address */
    cst_drv.raw_data[0] = 0x00;
}

void cst8xx_enable(bool en)
{
    /* No sleep command is known for this part -- the vendor driver never issues
     * one, it drops the rail instead. The reset line and the interrupt belong
     * to the board, so the honest subset here is to park the decoded state,
     * which stops a stale touch surviving a disable/enable cycle. */
    if(!en)
        cst8xx_park();
}

void cst8xx_irq_handler(void)
{
    /* We don't care if this fails, there's not much we can do about it */
    i2c_async_queue(CST8XX_BUS, TIMEOUT_NOBLOCK, I2C_Q_ONCE,
                    cst_drv.i2c_cookie, &cst_drv.i2c_desc);
}
