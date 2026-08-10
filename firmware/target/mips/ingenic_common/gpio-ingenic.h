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

/* GPIO API, shared by the X1000 and the X1600 -- the header half of
 * ingenic_common/gpio-ingenic.c.
 *
 * ⚠ NOT to be included directly. Each target's gpio-<soc>.h includes its own
 * generated register header FIRST and then this one, because everything below
 * is written against register names (GPIO_PAT0, GPIO_MSK, GPIO_PULL, ...) that
 * only the register header defines. The #error below turns a wrong include
 * order into one message instead of a wall of undefined macros.
 *
 * The register LAYOUTS differ in ways that do not reach this file: the X1600
 * puts PxINT at 0x10 rather than 0x00, adds PxEDG at 0x70, and moves the pull
 * enable to 0x80. Each target's generated header absorbs that and keeps the
 * X1000's register names, which is what makes one API possible.
 */

#ifndef __GPIO_INGENIC_H__
#define __GPIO_INGENIC_H__

/* REG_GPIO_PAT0, not GPIO_PAT0: the bare names below are tokens the jz_* macros
 * paste onto, not macros themselves, so testing one would always fail. */
#ifndef REG_GPIO_PAT0
# error "include gpio-x1000.h / gpio-x1600.h, not gpio-ingenic.h directly"
#endif

#include "config.h"
#include <stdint.h>

/* GPIO port numbers.
 *
 * Not register definitions, so not in the generated x1000/ and x1600/ headers. */
#define GPIO_A  0
#define GPIO_B  1
#define GPIO_C  2
#define GPIO_D  3
#define GPIO_Z  7   /* shadow group */

/* Both parts encode the pin function in the same four registers, which is the
 * property this shared API rests on (X1600 PM 21.4.2.x):
 *
 *   INT MSK PAT1 PAT0   meaning
 *    0   0    0    0    device function 0
 *    0   0    0    1    device function 1
 *    0   0    1    0    device function 2
 *    0   0    1    1    device function 3
 *    0   1    0    0    GPIO output, driving low
 *    0   1    0    1    GPIO output, driving high
 *    0   1    1    x    GPIO input
 *    1   0    0    0    IRQ, low level
 *    1   0    0    1    IRQ, high level
 *    1   0    1    0    IRQ, falling edge
 *    1   0    1    1    IRQ, rising edge
 *    1   1    x    x    IRQ, masked
 */

/* GPIO function bits, used to build the GPIOF_* values below.
 *
 * GPIO_F_PULL maps to PxPU. On the X1600 this is an ENABLE, not a disable: PM
 * 21.4.2.19 gives "0: HI-Z, 1: PULL-UP/DOWN Enable", so GPIOF_INPUT enables the
 * pad's pull and GPIOF_DEVICE(i) leaves peripheral pins Hi-Z. The pull
 * DIRECTION is fixed in the pad and is not selectable.
 *
 * The X1000's sense is not documented anywhere in this tree. Sharing does not
 * introduce that gap: gpio-x1000.c already set and cleared this bit exactly the
 * same way, so each target behaves as it did before. But do not treat the X1600
 * PM citation above as evidence about the X1000. */
#define GPIO_F_PULL 16
#define GPIO_F_INT  8
#define GPIO_F_MASK 4
#define GPIO_F_PAT1 2
#define GPIO_F_PAT0 1

/* GPIO function numbers */
#define GPIOF_DEVICE(i)     ((i)&3)
#define GPIOF_OUTPUT(i)     (0x4|((i)&1))
#define GPIOF_INPUT         0x16
#define GPIOF_IRQ_LEVEL(i)  (0x1c|((i)&1))
#define GPIOF_IRQ_EDGE(i)   (0x1e|((i)&1))

/* GPIO pin numbers. Two bits of port: both parts have exactly four real
 * ports, PA..PD. */
#define GPION_CREATE(port, pin) ((((port) & 3) << 5) | ((pin) & 0x1f))
#define GPION_PORT(gpio)        (((gpio) >> 5) & 3)
#define GPION_PIN(gpio)         ((gpio) & 0x1f)
#define GPION_MASK(gpio)        (1u << GPION_PIN(gpio))

/* Easy pin number macros */
#define GPIO_PA(x)  GPION_CREATE(GPIO_A, x)
#define GPIO_PB(x)  GPION_CREATE(GPIO_B, x)
#define GPIO_PC(x)  GPION_CREATE(GPIO_C, x)
#define GPIO_PD(x)  GPION_CREATE(GPIO_D, x)

/* GPIO number to IRQ number (need to include the target's irq-<soc>.h) */
#define GPIO_TO_IRQ(gpio) IRQ_GPIO(GPION_PORT(gpio), GPION_PIN(gpio))

/* Pingroup settings are used for system devices */
struct pingroup_setting {
    int port;
    uint32_t pins;
    int func;
};

/* GPIO settings are used for single pins under software control */
struct gpio_setting {
    int gpio;
    int func;
};

/* Target pins are defined as GPIO_XXX constants usable with the GPIO API */
enum {
#define DEFINE_GPIO(_name, _gpio, _func) GPIO_##_name = _gpio,
#define DEFINE_PINGROUP(...)
#include "gpio-target.h"
#undef DEFINE_GPIO
#undef DEFINE_PINGROUP
    GPIO_NONE = -1,
};

/* These are pin IDs which index gpio_settings */
enum {
#define DEFINE_GPIO(_name, ...) PIN_##_name,
#define DEFINE_PINGROUP(...)
#include "gpio-target.h"
#undef DEFINE_GPIO
#undef DEFINE_PINGROUP
    PIN_COUNT,
};

/* Pingroup IDs which index pingroup_settings */
enum {
#define DEFINE_GPIO(...)
#define DEFINE_PINGROUP(_name, ...) PINGROUP_##_name,
#include "gpio-target.h"
#undef DEFINE_GPIO
#undef DEFINE_PINGROUP
    PINGROUP_COUNT,
};

/* called at early init to set up GPIOs */
extern void gpio_init(void) INIT_ATTR;

/* Use the PZ shadow group to reconfigure several pins atomically */
extern void gpioz_configure(int port, uint32_t pins, int func);

static inline void gpio_set_function(int gpio, int func)
{
    gpioz_configure(GPION_PORT(gpio), GPION_MASK(gpio), func);
}

static inline int gpio_get_level(int gpio)
{
    return REG_GPIO_PIN(GPION_PORT(gpio)) & GPION_MASK(gpio) ? 1 : 0;
}

static inline void gpio_set_level(int gpio, int value)
{
    if(value)
        jz_set(GPIO_PAT0(GPION_PORT(gpio)), GPION_MASK(gpio));
    else
        jz_clr(GPIO_PAT0(GPION_PORT(gpio)), GPION_MASK(gpio));
}

/* Enable or disable the pad's pull resistor -- see GPIO_F_PULL above for the
 * polarity, and note the direction is fixed in silicon, not selectable. */
static inline void gpio_set_pull(int gpio, int state)
{
    if(state)
        jz_set(GPIO_PULL(GPION_PORT(gpio)), GPION_MASK(gpio));
    else
        jz_clr(GPIO_PULL(GPION_PORT(gpio)), GPION_MASK(gpio));
}

static inline void gpio_mask_irq(int gpio, int mask)
{
    if(mask)
        jz_set(GPIO_MSK(GPION_PORT(gpio)), GPION_MASK(gpio));
    else
        jz_clr(GPIO_MSK(GPION_PORT(gpio)), GPION_MASK(gpio));
}

#define gpio_set_irq_level      gpio_set_level
#define gpio_enable_irq(gpio)   gpio_mask_irq((gpio), 0)
#define gpio_disable_irq(gpio)  gpio_mask_irq((gpio), 1)

/* Helper function for edge-triggered IRQs when you want to get an
 * interrupt on both the rising and falling edges, using only the
 * single-edge hardware.
 *
 * Despite the name, this doesn't depend on the currently set edge,
 * it just reads the GPIO state and sets up an edge trigger to detect
 * a change to the other state -- if some transitions were missed the
 * IRQ trigger may remain unchanged.
 *
 * It can be safely used to initialize the IRQ level.
 */
static inline void gpio_flip_edge_irq(int gpio)
{
    if(gpio_get_level(gpio))
        gpio_set_irq_level(gpio, 0);
    else
        gpio_set_irq_level(gpio, 1);
}

/* Number of real GPIO ports, not counting the PZ shadow group. Used by
 * gpio-ingenic.c; PA..PD on both parts. */
#define GPIO_NUM_PORTS 4

#endif /* __GPIO_INGENIC_H__ */
