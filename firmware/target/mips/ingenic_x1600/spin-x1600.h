/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> >    <
 *   Player     |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Bounded spinning for X1600 hardware waits.
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
#ifndef __SPIN_X1600_H__
#define __SPIN_X1600_H__

#include <stdint.h>

/* An unbounded `while(!(REG & BIT));` is the most effective way to lose this
 * device: no watchdog runs during boot, so the only recovery is a power cycle,
 * and in the SPL that is true on every boot.
 *
 * The guard is an iteration count, not a time: the time base is exactly what
 * cannot be trusted here. See __ost_delay() in system-target.h.
 *
 * The budget is ~100 ms at any plausible boot clock -- far longer than these
 * bits legitimately need, and still finite when one is dead. */
#define X1600_SPIN_GUARD 2000000u

/* Spin while `cond` is true. Non-zero if the guard expired, ie. the condition
 * never cleared. Callers that can report an error should. */
#define x1600_spin_while(cond)                                              \
    __extension__ ({                                                        \
        uint32_t _spin_guard = X1600_SPIN_GUARD;                            \
        while((cond) && --_spin_guard)                                      \
            ;                                                               \
        _spin_guard == 0;                                                   \
    })

/* Spin until `cond` becomes true. Non-zero if the guard expired. */
#define x1600_spin_until(cond) x1600_spin_while(!(cond))

/* Same, but run `body` each pass, for waits that must yield to the kernel.
 * Only usable once the scheduler is running -- not in the SPL. */
#define x1600_spin_while_do(cond, body)                                     \
    __extension__ ({                                                        \
        uint32_t _spin_guard = X1600_SPIN_GUARD;                            \
        while((cond) && --_spin_guard)                                      \
            { body; }                                                       \
        _spin_guard == 0;                                                   \
    })

#endif /* __SPIN_X1600_H__ */
