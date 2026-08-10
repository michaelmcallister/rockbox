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
 * Based on firmware/target/mips/ingenic_x1000/gpio-x1000.h,
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

#ifndef __GPIO_X1600_H__
#define __GPIO_X1600_H__

/* Register definitions first: gpio-ingenic.h is written against these names
 * and checks that they are present. */
#include "x1600/gpio.h"
#include "gpio-ingenic.h"

/* The register OFFSETS are not the X1000's -- PxINT is at 0x10, PxEDG at 0x70
 * is new, and PxPU moved to 0x80 -- but x1600/gpio.h keeps the X1000's register
 * NAMES, so the shared API above works unchanged.
 *
 * Pull direction on this part is fixed in the pad and is not selectable: PM
 * 21.4.2.19 records that PC24, PC25 and PC26 pull DOWN and every other pad
 * pulls UP.  MSC1 (microSD) is a GPIOF_DEVICE pingroup and nothing here calls
 * gpio_set_pull(), so any pull on the card's CMD/DAT lines is external.
 */

/* The X1600 CAN interrupt on both edges in hardware and this port does not use
 * it.  PM 21.4.2.16 (PxEDG, offset 0x70): "0 = single edge, edge chosen by
 * PAT0; 1 = dual edge", and the pin must already be GPIOF_IRQ_EDGE(x).  The
 * X1000 has no such register at all.
 *
 * Both edge sources this port needs (PA16 touch, PB22 card detect) use
 * gpio_flip_edge_irq() instead -- the X1000's approach, and the only one
 * available in shared code.  The register detail is kept because it is why the
 * shared API looks the way it does, and re-deriving it costs a PM lookup
 * keeping to preserve it.
 */

#endif /* __GPIO_X1600_H__ */
