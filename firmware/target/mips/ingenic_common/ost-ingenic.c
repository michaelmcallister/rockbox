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

/* The slow-path delay API, shared by the X1000 and the X1600.
 *
 * These are shareable precisely because the primitive they spin on is NOT:
 * __ost_delay() comes from each target's system-target.h, and the X1600's is
 * bounded where the X1000's is not. These three only chunk the argument and
 * read the counter.
 */

#include "system.h"
#include "ingenic-soc.h"

void __udelay(uint32_t us)
{
    while(us > MAX_UDELAY_ARG) {
        __ost_delay(MAX_UDELAY_ARG * OST_TICKS_PER_US);
        us -= MAX_UDELAY_ARG;
    }

    __ost_delay(us * OST_TICKS_PER_US);
}

void __mdelay(uint32_t ms)
{
    while(ms > MAX_MDELAY_ARG) {
        __ost_delay(MAX_MDELAY_ARG * 1000 * OST_TICKS_PER_US);
        ms -= MAX_MDELAY_ARG;
    }

    __ost_delay(ms * 1000 * OST_TICKS_PER_US);
}

uint64_t __ost_read64(void)
{
    /* Reading OST2CNTL latches the high half; do not let an IRQ split them */
    int irq = disable_irq_save();
    uint64_t lcnt = REG_OST_2CNTL;
    uint64_t hcnt = REG_OST_2CNTHB;
    restore_irq(irq);
    return (hcnt << 32) | lcnt;
}
