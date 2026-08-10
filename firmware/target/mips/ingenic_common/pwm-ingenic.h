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

/* PWM interface, shared by the X1000 and the X1600.
 *
 * The IMPLEMENTATIONS are not shared and cannot be: the X1000 drives PWM from
 * TCU timer channels, while the X1600's TCU lost PWM_EN, PWM_IN_EN, INIT_LVL,
 * CLRZ and BYPASS from TCSR and the part has a dedicated 8-channel PWM block
 * instead. Channel numbers therefore mean different things per target -- see
 * each pwm-*.h.
 *
 * That is the argument FOR pinning the interface, not against it: four board
 * files consume these declarations (fiiom3k, shanlingq1, erosqnative, hibyr1),
 * and two implementations claiming one API is the reason to write the API down
 * once rather than leave the match to convention.
 *
 * Usage:
 * - Call pwm_init(n) before using channel n
 * - Call pwm_set_period() to change the period and duty cycle at any time
 * - Call pwm_enable() and pwm_disable() to turn the output on and off
 * - Don't allow two threads to control the same channel at the same time
 * - Don't call pwm_init(), pwm_enable(), or pwm_disable() from an interrupt
 *
 * After calling pwm_init(), the channel is essentially in a disabled state so
 * you will need to call pwm_set_period() and then pwm_enable() to turn it on.
 * Don't alter the channel's TCU or GPIO pin state after calling pwm_init().
 *
 * After calling pwm_disable(), it is safe to use the channel's TCU or GPIO pin
 * for some other purpose, but you must call pwm_init() before you can use the
 * channel as PWM output again.
 */

#ifndef __PWM_INGENIC_H__
#define __PWM_INGENIC_H__

extern void pwm_init(int chn);
extern void pwm_set_period(int chn, int period_ns, int duty_ns);
extern void pwm_enable(int chn);
extern void pwm_disable(int chn);

#endif /* __PWM_INGENIC_H__ */
