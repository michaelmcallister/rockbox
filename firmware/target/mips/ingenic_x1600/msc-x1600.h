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
 * Based on firmware/target/mips/ingenic_x1000/msc-x1000.h,
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

#ifndef __MSC_X1600_H__
#define __MSC_X1600_H__

/* Self-contained: a header that is reached without config.h and tests a
 * config value by VALUE compiles as 0 silently. That happened here with
 * X1600_MSC_BOUNCE and was caught by -Wundef. */
#include "config.h"

/* The X1600 MSC is the same IP as the X1000's: the two generated register
 * headers are byte-identical apart from their include guards. Both the
 * interface and the driver are therefore shared (ingenic_common/msc-ingenic.[ch])
 * and this file carries only what is genuinely this SoC's or this board's:
 *
 *   1. R1: the microSD is on MSC1 (PD00-PD05); MSC0 is the SDIO wifi. Every
 *      X1000 target puts the card on MSC0 -- do not copy them.
 *   2. X1600: MSC1CDR has its own 2-bit source mux (MPCS, 00=SCLK_A/01=MPLL/
 *      10=EPLL) where X1000's MSC1 borrows MSC0CDR.CLKSRC, and the field names
 *      differ throughout.
 *   3. R1: card power is a GPIO (PC25, active high), card detect PB22.
 *
 * 1 and 2 live in msc-x1600.c behind the hooks the shared header declares.
 */

/* Register definitions first: the shared driver is written against these
 * names and reaches them only through ingenic-soc.h. */
#include "x1600/msc.h"

/* Difference 3 above: adds pwr_gpio/pwr_active_level and the pin-group fields
 * to msc_config.  Must be defined BEFORE the shared header, which consumes it
 * inside the struct. */
#define SOC_MSC_HAS_BOARD_PINS 1

#include "msc-ingenic.h"


#endif /* __MSC_X1600_H__ */
