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

#include "power.h"
#include "powermgmt.h"
#include "adc.h"
#include "system.h"
#include <limits.h>
#include "kernel.h"
#include "axp-2101.h"
#include "usb-x1600.h"
#include "button-target.h"
#include <stdint.h>
#ifdef HAVE_CW2015
# include "cw2015.h"
#endif
#ifdef HAVE_USB_CHARGING_ENABLE
# include "usb_core.h"
#endif

#include "i2c-ingenic.h"
#include "x1600/sadc.h"
#include "x1600/cpm.h"

/* Single-cell Li-ion, Vmax 4.35 V. Percentages come from the CW2015 gauge;
 * these curves serve only the low-battery logic. */

/* Unverified, copied from the X1000 ports: nobody has watched an R1 run flat */
unsigned short battery_level_disksafe = 3470;
unsigned short battery_level_shutoff = 3400;

/* PLACEHOLDER CURVES: the Eros Q Native's, for a 4.2 V cell, so everything
 * above ~4.1 V clamps to 100%. Do not scale them -- a Li-ion curve is not
 * linear. Replacing them needs a full rundown logging voltage and SOC. */
/* voltages (millivolt) of 0%, 10%, ... 100% when charging disabled */
unsigned short percent_to_volt_discharge[11] =
{
    3400, 3477, 3540, 3578, 3617, 3674, 3771, 3856, 3936, 4016, 4117
};

/* voltages (millivolt) of 0%, 10%, ... 100% when charging enabled */
unsigned short percent_to_volt_charge[11] =
{
    3400, 3477, 3540, 3578, 3617, 3674, 3771, 3856, 3936, 4016, 4117
};

/* AXP2101. Voltages are the vendor's own work_voltage per regulator, read off
 * the live device; dcdc5 and vcpus are not populated, dcdc1/2/3/4 and aldo3 are
 * system rails, rtcldo feeds the RTC. Known consumers are marked below. */
#define R1_MV_DCDC1     3300
#define R1_MV_DCDC2     1100
#define R1_MV_DCDC3     1800
#define R1_MV_DCDC4     1200
#define R1_MV_ALDO1     3300
#define R1_MV_ALDO2     3300
#define R1_MV_ALDO3     1100
#define R1_MV_ALDO4     3300
#define R1_MV_BLDO1     2800
#define R1_MV_BLDO2     2800
#define R1_MV_DLDO1     3300
#define R1_MV_DLDO2     1250

/* adc_read_mv() lives here rather than in sadc-x1600.c because the millivolt
 * scaling is board data: it depends on this board's reference and dividers,
 * not on the controller */

int adc_read_mv(int channel)
{
    return ADC_TO_MV(adc_read(channel));
}

/* Power */

void power_init(void)
{
    i2c_ingenic_set_freq(AXP_PMU_BUS, I2C_FREQ_400K);
    axp2101_init();
#ifdef HAVE_CW2015
    cw2015_init();
#endif

    /* Enable the ADCs we care about */
    axp2101_adc_set_enabled(
        (1 << AXP2101_ADC_VBAT_VOLTAGE) |
        (1 << AXP2101_ADC_VBUS_VOLTAGE) |
        (1 << AXP2101_ADC_VSYS_VOLTAGE) |
        (1 << AXP2101_ADC_DIE_TEMPERATURE));

    /* axp2101_supply_set_voltage() also enables the rail */
    axp2101_supply_set_voltage(AXP2101_SUPPLY_DCDC1, R1_MV_DCDC1);
    axp2101_supply_set_voltage(AXP2101_SUPPLY_DCDC2, R1_MV_DCDC2);
    axp2101_supply_set_voltage(AXP2101_SUPPLY_DCDC3, R1_MV_DCDC3);
    axp2101_supply_set_voltage(AXP2101_SUPPLY_DCDC4, R1_MV_DCDC4);
    axp2101_supply_set_voltage(AXP2101_SUPPLY_ALDO3, R1_MV_ALDO3);

    /* Rails with a known consumer in this port */
    axp2101_supply_set_voltage(AXP2101_SUPPLY_ALDO2, R1_MV_ALDO2); /* touch */
    axp2101_supply_set_voltage(AXP2101_SUPPLY_ALDO4, R1_MV_ALDO4); /* remote */
    axp2101_supply_set_voltage(AXP2101_SUPPLY_BLDO1, R1_MV_BLDO1); /* LCD VCC */
    axp2101_supply_set_voltage(AXP2101_SUPPLY_BLDO2, R1_MV_BLDO2); /* LCD IO */

    /* aldo1, dldo1 and dldo2 are populated but it is unknown what they feed.
     * The vendor leaves them off at boot, so this does too. */

    /* Three vendor charger settings the driver cannot express, left at the
     * chip's defaults: charge_voltage_limit, charge_term_current and
     * dcdc3_always_pwmmode, whose encodings are unknown */

    /* Policy, not measurement: the vendor does not expose its charging
     * current, so use what a plain USB 2.0 port allows */
    axp2101_set_charge_current(500);

    /* Let the rails settle before anything talks to the panel or DAC */
    mdelay(20);
}

#ifdef HAVE_USB_CHARGING_ENABLE
void usb_charging_maxcurrent_change(int maxcurrent)
{
    axp2101_set_charge_current(maxcurrent);
}
#endif

void power_off(void)
{
    axp2101_power_off();
    while(1);
}

/* Do NOT report POWER_INPUT_BATTERY: power.h defines it only under
 * HAVE_BATTERY_SWITCH, which this soldered-in battery does not set */
unsigned int power_input_status(void)
{
#if defined(HAVE_USBSTACK) && !defined(USB_NONE)
    /* Thread context: the only place the cached USB status is refreshed */
    x1600_usb_refresh_detect();
#endif
    return charging_state() ? POWER_INPUT_USB_CHARGER : POWER_INPUT_NONE;
}

bool charging_state(void)
{
    return axp2101_battery_status() == AXP2101_BATT_CHARGING;
}

int _battery_voltage(void)
{
    /* The CW2015 can also report cell voltage but on the Shanling Q1
     * the AXP consistently read 20-30 mV higher and was taken as the
     * truth; assume the same here until measured */
    static int last_mv = 3700; /* mid-discharge; used only before the first
                                * successful read, and never as a real sample */
    int mv = axp2101_adc_read(AXP2101_ADC_VBAT_VOLTAGE);

    /* INT_MIN means the I2C read failed, and this feeds query_force_shutdown().
     * Reporting it verbatim is a cell at -2147483648 mV, i.e. critically flat,
     * from a PMU that NAKs occasionally -- see the VBUS note in usb-x1600.c.
     * Hold the last good sample rather than inventing a catastrophic one. */
    if(mv != INT_MIN)
        last_mv = mv;

    return last_mv;
}

#if (CONFIG_BATTERY_MEASURE & CURRENT_MEASURE) != 0
int _battery_current(void)
{
    /* hibyr1native.h declares CURRENT_MEASURE but neither the AXP2101 nor the
     * CW2015 has a current channel. The real fix is to drop it. */
    return -1;
}
#endif

#if defined(HAVE_CW2015) && (CONFIG_BATTERY_MEASURE & PERCENTAGE_MEASURE) != 0
int _battery_level(void)
{
    return cw2015_get_soc();
}
#endif

#if defined(HAVE_CW2015) && (CONFIG_BATTERY_MEASURE & TIME_MEASURE) != 0
int _battery_time(void)
{
    return cw2015_get_rrt();
}
#endif
