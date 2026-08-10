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

#ifndef __MSC_X1000_H__
#define __MSC_X1000_H__

/* Same MSC IP as the X1600, so both the interface and the driver are shared;
 * see ingenic_common/msc-ingenic.h.  msc-x1000.c keeps only the controller
 * table and the clock hooks. */

/* Register definitions first: the shared driver is written against these
 * names and reaches them only through ingenic-soc.h. */
#include "x1000/msc.h"
#include "msc-ingenic.h"

#endif /* __MSC_X1000_H__ */
