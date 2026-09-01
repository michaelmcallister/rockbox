/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
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

#include "pwm-x1600.h"
#include "x1600/pwm.h"
#include "x1600/cpm.h"

/* Stubs so the device layer links, rather than poking registers with
 * unverified values. To implement: ungate (CLKGR and PWMCDR), mux the pin
 * (backlight = PC00), then set period and duty from the measured peripheral
 * clock -- Linux reports EPLL at 300 MHz -- never from an assumed rate. */

void pwm_init(int chn)
{
    (void)chn;
}

void pwm_set_period(int chn, int period_ns, int duty_ns)
{
    (void)chn; (void)period_ns; (void)duty_ns;
}

void pwm_enable(int chn)
{
    (void)chn;
}

void pwm_disable(int chn)
{
    (void)chn;
}
