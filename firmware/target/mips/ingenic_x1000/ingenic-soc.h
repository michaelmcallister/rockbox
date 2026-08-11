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

#ifndef __INGENIC_SOC_H__
#define __INGENIC_SOC_H__

/* SOC glue for the drivers in target/mips/ingenic/.
 *
 * Every target with one of those drivers supplies this header, naming its own
 * register headers so that a shared driver never has to name one SOC's.  It
 * carries what the drivers shared so far need, and grows as more of them are
 * split out.
 */

#include "irq-x1000.h"
#include "clk-x1000.h"
#include "x1000/cpm.h"
#include "x1000/msc.h"

#endif /* __INGENIC_SOC_H__ */
