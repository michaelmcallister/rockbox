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

#include "button.h"
#include "touchscreen.h"
#include "cst8xx.h"
#include "kernel.h"
#include "system.h"
#include "adc.h"
#include "gpio-x1600.h"
#include "irq-x1600.h"
#include "i2c-ingenic.h"
#include <stdbool.h>

/* HiBy R1 buttons.
 *
 * Five keys. That is all there is:
 *
 *   POWER     GPIO PC31, active low   (SoC WKUP_ pad, wakeup source)
 *   NEXT      GPIO PC28, active high  (also the BOOT_SEL1 strap)
 *   PLAY      SADC AUX0 ladder, 0 mV
 *   VOL_DOWN  SADC AUX0 ladder, 1000 mV
 *   VOL_UP    SADC AUX0 ladder, 1650 mV
 *
 * THERE IS NO PREV KEY. Not "we haven't found it yet" - the board does
 * not have one. The vendor input configuration has exactly two GPIO
 * keys and exactly three populated ladder entries (key4..key8 are all
 * -1), and the ladder's idle level is 3300 mV. Anything that wants
 * "previous track" has to get it from a long press or the touchscreen.
 *
 * Because PC28 doubles as BOOT_SEL1, holding NEXT while plugging in USB
 * lands the SoC in BootROM USB mode instead of booting. That is a
 * hardware property, not something this driver can help with, but it is
 * the recovery path if a bad build is flashed.
 */

/* ======================================================================
 * SADC key ladder
 * ====================================================================== */

/* The OF debounces the ladder over adc_key_detectime = 100 ms. Rockbox's
 * button tick is 100 Hz, so N consecutive agreeing samples is N*10 ms.
 * Two is enough to reject a single bad sample while staying responsive.
 *
 * TODO(x1600): if phantom key presses show up (e.g. while charging),
 * raise this towards 10 to match the OF exactly.
 *
 * The idle level was measured at 3221-3259 mV across 15 samples on hardware
 *, against the 3300 mV nominal -- comfortably inside the +/-100 mV
 * window, so the decode has margin. */
#define ADC_KEY_AGREE_COUNT 2

static int adc_key_last = 0;
static int adc_key_agree = 0;
static int adc_key_stable = 0;

static inline bool adc_near(int mv, int target)
{
    return mv >= (target - ADC_KEY_WINDOW_MV) &&
           mv <= (target + ADC_KEY_WINDOW_MV);
}

static int adc_key_decode(int mv)
{
    /* Idle first: 3300 mV means nothing is pressed. Checking it before
     * the key windows matters because the idle level is only 1650 mV
     * above VOL_UP's nominal and a wide window could otherwise
     * overlap. (It does not with +/-100 mV, but be explicit.) */
    if(adc_near(mv, ADC_KEY_IDLE_MV))
        return 0;

    if(adc_near(mv, ADC_KEY_PLAY_MV))
        return BUTTON_PLAY;
    if(adc_near(mv, ADC_KEY_VOLDOWN_MV))
        return BUTTON_VOL_DOWN;
    if(adc_near(mv, ADC_KEY_VOLUP_MV))
        return BUTTON_VOL_UP;

    /* Between windows: mid-press or noise, hold the previous state. */
    return -1;
}

static int adc_key_read(void)
{
    int key = adc_key_decode(adc_read_mv(ADC_BUTTONS));

    if(key < 0)
        return adc_key_stable;

    if(key == adc_key_last) {
        if(adc_key_agree < ADC_KEY_AGREE_COUNT)
            ++adc_key_agree;
    } else {
        adc_key_last = key;
        adc_key_agree = 1;
    }

    if(adc_key_agree >= ADC_KEY_AGREE_COUNT)
        adc_key_stable = key;

    return adc_key_stable;
}

/* ======================================================================
 * Rockbox interface
 * ====================================================================== */

void button_init_device(void)
{
    i2c_ingenic_set_freq(CST8XX_BUS, I2C_FREQ_400K);
    cst8xx_init();

    /* Reset the touch controller. The vendor driver has no power GPIO
     * for it (cst_power_en_gpio = -1); the panel is powered from
     * aldo2, which power_init() has already brought up.
     * TODO(x1600): the vendor reset pulse width is not recorded in the
     * module parameters. 5 ms low then 50 ms to boot is the CST8xx
     * datasheet-typical figure and matches what the Shanling Q1 port
     * does for its FT6x06; it has not been verified on this part. */
    gpio_set_level(GPIO_CST8XX_RESET, 0);
    mdelay(5);
    gpio_set_level(GPIO_CST8XX_RESET, 1);
    mdelay(50);

    /* Touch data is IRQ-driven, which is the primary defence against
     * reading a half-updated register block. */
    system_set_irq_handler(GPIO_TO_IRQ(GPIO_CST8XX_INTERRUPT),
                           cst8xx_irq_handler);
    gpio_set_function(GPIO_CST8XX_INTERRUPT, GPIOF_IRQ_EDGE(0));
    gpio_enable_irq(GPIO_CST8XX_INTERRUPT);
}


int button_read_device(int* data)
{
    int r = 0;

    uint32_t c = REG_GPIO_PIN(GPIO_C);

    /* POWER is active LOW (PC31 is the SoC's WKUP_ pad). */
    if((c & (1 << 31)) == 0)
        r |= BUTTON_POWER;

    /* NEXT is active HIGH (PC28, shared with the BOOT_SEL1 strap). */
    if(c & (1 << 28))
        r |= BUTTON_NEXT;

    /* PLAY / VOL+ / VOL- come off the SADC ladder. */
    r |= adc_key_read();

    /* Touch. The panel is single-touch, so 3x3 grid mode and point mode
     * differ only in whether the caller wants the coordinates back. */
    const struct cst8xx_point* point = &cst8xx_state.points[0];
    if(cst8xx_state.nr_points > 0 &&
       (point->event == CST8XX_EVT_DOWN ||
        point->event == CST8XX_EVT_CONTACT)) {
        int* out = (touchscreen_get_mode() == TOUCHSCREEN_POINT) ? data : NULL;
        r |= touchscreen_to_pixels(point->pos_x, point->pos_y, out);
    }


    return r;
}

void touchscreen_enable_device(bool en)
{
    /* TODO(x1600): the CST8xx sleep command is not known - the vendor
     * driver never issues one, it just drops aldo2. Holding reset is
     * the honest subset: it stops the controller asserting PA16, which
     * is what "disabled" has to mean at minimum.
     *
     * Dropping aldo2 would save more power but the driver would then
     * have to re-run the reset sequence on re-enable, and we have no
     * evidence about how long the part needs. */
    gpio_set_level(GPIO_CST8XX_RESET, en ? 1 : 0);

    if(en) {
        mdelay(50);
        gpio_enable_irq(GPIO_CST8XX_INTERRUPT);
    } else {
        gpio_disable_irq(GPIO_CST8XX_INTERRUPT);
    }

    /* Parks the decoded state on disable, so a touch held across the
     * transition cannot survive it. */
    cst8xx_enable(en);
}

bool headphones_inserted(void)
{
    /* SADC AUX1, "inserted" when the divider reads 2800-3300 mV.
     * From the vendor sa_sound_switch parameters
     * (sass_headset_adc_channel=1, _min=2800, _max=3300,
     * _range_state=1, i.e. in-range means asserted).
     *
     * The vendor debounces this over 200 ms; adc_read() here returns a
     * cached sample and Rockbox polls this from its own tick, so a
     * single glitch could flap the state.
     * With a jack fully inserted: 3273-3299 mV across 9
     * samples, a 26 mV spread well inside the 2800-3300 mV window, with no
     * flapping. So the settled case needs no debounce.
     * TODO(x1600): the HALF-inserted case is still unwatched, and that is the
     * one that would flap. Add a counter only if it does. */
    int mv = adc_read_mv(ADC_HP_DETECT);
    return mv >= ADC_HP_MIN_MV && mv <= ADC_HP_MAX_MV;
}
