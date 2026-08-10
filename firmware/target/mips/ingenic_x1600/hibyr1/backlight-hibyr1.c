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

#include "backlight.h"
#include "backlight-target.h"
#include "lcd.h"
#include "pwm-x1600.h"
#include "gpio-x1600.h"

/* The vendor's settings, off the live device: PWM channel 0 on PC00 at 30 kHz,
 * active high, no enable pin, 101 steps */

#define BL_LCD_CHN      0
#define BL_LCD_PERIOD   33333   /* nanoseconds, = 30 kHz */

/* The pin is a GPIO because pwm-x1600.c is stubs, so the backlight is on/off.
 * The config header no longer claims a brightness range it cannot deliver --
 * see hibyr1native.h -- which is why no brightness entry point lives here.
 *
 * Writing pwm-x1600.c is not enough alone: CLKGR1 resets with the PWM bit set
 * and nothing here ungates it, so its registers read garbage with writes
 * swallowed -- the INTC failure again. Switch the pin back to GPIOF_DEVICE(0)
 * at the same time, or the PWM drives a pin the GPIO block still owns. */
#define BL_GPIO     GPIO_PC(0)      /* pwm0_o, gpio-target.h PINGROUP(PWM0) */

bool backlight_hw_init(void)
{
    pwm_init(BL_LCD_CHN);
    pwm_enable(BL_LCD_CHN);

    /* Take the pad away from the PWM and drive it high */
    gpio_set_function(BL_GPIO, GPIOF_OUTPUT(1));
    return true;
}

void backlight_hw_on(void)
{
    pwm_enable(BL_LCD_CHN);
    gpio_set_function(BL_GPIO, GPIOF_OUTPUT(1));
#ifdef HAVE_LCD_ENABLE
    lcd_enable(true);
#endif
}

void backlight_hw_off(void)
{
    pwm_disable(BL_LCD_CHN);
    gpio_set_function(BL_GPIO, GPIOF_OUTPUT(0));
#ifdef HAVE_LCD_ENABLE
    lcd_enable(false);
#endif
}
