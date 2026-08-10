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

#ifndef BOOTLOADER
#include "system.h"
#include "debug-ingenic.h"
#include "kernel.h"
#include "button.h"
#include "lcd.h"
#include "font.h"
#include "action.h"
#include "list.h"

#include "clk-x1000.h"
#include "gpio-x1000.h"

static bool dbg_clocks(void)
{
    do {
        lcd_clear_display();
        int line = 0;
        for(int i = 0; i < X1000_CLK_COUNT; ++i) {
            uint32_t hz = clk_get(i);
            uint32_t khz = hz / 1000;
            uint32_t mhz = khz / 1000;
            lcd_putsf(2, line++, "%8s  %4u,%03u,%03u Hz", clk_get_name(i),
                      mhz, (khz - mhz*1000), (hz - khz*1000));
        }

        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

static void dbg_gpios_show_state(void)
{
    const char portname[] = "ABCD";
    for(int i = 0; i < 4; ++i)
        lcd_putsf(0, i, "GPIO %c: %08x", portname[i], REG_GPIO_PIN(i));
}

static void dbg_gpios_show_config(void)
{
    const char portname[] = "ABCD";
    int line = 0;
    for(int i = 0; i < 4; ++i) {
        uint32_t intr = REG_GPIO_INT(i);
        uint32_t mask = REG_GPIO_MSK(i);
        uint32_t pat0 = REG_GPIO_PAT0(i);
        uint32_t pat1 = REG_GPIO_PAT1(i);
        lcd_putsf(0, line++, "GPIO %c", portname[i]);
        lcd_putsf(2, line++, " int %08lx", intr);
        lcd_putsf(2, line++, " msk %08lx", mask);
        lcd_putsf(2, line++, "pat0 %08lx", pat0);
        lcd_putsf(2, line++, "pat1 %08lx", pat1);
        line++;
    }
}

static bool dbg_gpios(void)
{
    enum { STATE, CONFIG, NUM_SCREENS };
    const int timeouts[NUM_SCREENS] = { 1, HZ };
    int screen = STATE;

    while(1) {
        lcd_clear_display();
        switch(screen) {
        case CONFIG:
            dbg_gpios_show_config();
            break;
        case STATE:
            dbg_gpios_show_state();
            break;
        }

        lcd_update();

        switch(get_action(CONTEXT_STD, timeouts[screen])) {
        case ACTION_STD_CANCEL:
            return false;
        case ACTION_STD_PREV:
        case ACTION_STD_PREVREPEAT:
            screen -= 1;
            if(screen < 0)
                screen = NUM_SCREENS - 1;
            break;
        case ACTION_STD_NEXT:
        case ACTION_STD_NEXTREPEAT:
            screen += 1;
            if(screen >= NUM_SCREENS)
                screen = 0;
            break;
        default:
            break;
        }
    }

    return false;
}

extern volatile unsigned aic_tx_underruns;
#ifdef HAVE_RECORDING
extern volatile unsigned aic_rx_overruns;
#endif
#ifdef HAVE_EROS_QN_CODEC
extern int es9018k2m_present_flag;
#endif

static bool dbg_audio(void)
{
    do {
        lcd_clear_display();
        lcd_putsf(0, 0, "TX underruns: %u", aic_tx_underruns);
#ifdef HAVE_RECORDING
        lcd_putsf(0, 1, "RX overruns:  %u", aic_rx_overruns);
#endif
#ifdef HAVE_EROS_QN_CODEC
        if (es9018k2m_present_flag)
        {
            lcd_putsf(0, 2, "(%d) ES9018K2M HWVOL", es9018k2m_present_flag);
        }
        else
        {
            lcd_putsf(0, 2, "(%d) SWVOL", es9018k2m_present_flag);
        }
#endif
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

#ifdef FIIO_M3K
extern bool dbg_fiiom3k_touchpad(void);
#endif
#ifdef SHANLING_Q1
extern bool dbg_shanlingq1_touchscreen(void);
#endif
#ifdef HAVE_AXP_PMU
extern bool axp_debug_menu(void);
#endif
#ifdef HAVE_CW2015
extern bool cw2015_debug_menu(void);
#endif

/* Menu definition */
const struct ingenic_debug_menuitem ingenic_debug_menu[] = {
    {"Clocks", &dbg_clocks},
    {"GPIOs", &dbg_gpios},
#ifdef INGENIC_CPUIDLE_STATS
    {"CPU idle", &dbg_cpuidle},
#endif
    {"Audio", &dbg_audio},
#ifdef FIIO_M3K
    {"Touchpad", &dbg_fiiom3k_touchpad},
#endif
#ifdef SHANLING_Q1
    {"Touchscreen", &dbg_shanlingq1_touchscreen},
#endif
#ifdef HAVE_AXP_PMU
    {"Power stats", &axp_debug_menu},
#endif
#ifdef HAVE_CW2015
    {"CW2015 debug", &cw2015_debug_menu},
#endif
};

const int ingenic_debug_menu_count = ARRAYLEN(ingenic_debug_menu);




bool dbg_ports(void)
{
    return false;
}
#endif
