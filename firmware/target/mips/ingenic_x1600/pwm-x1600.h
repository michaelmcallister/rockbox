/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
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

#ifndef __PWM_X1600_H__
#define __PWM_X1600_H__

#include "pwm-ingenic.h"

/* Channel numbers are the DEDICATED PWM block's, not the TCU's. The X1600 TCU's
 * TCSR has lost PWM_EN, PWM_IN_EN, INIT_LVL, CLRZ and BYPASS entirely, so
 * pwm-x1000.c -- which drives PWM from TCU timer channels -- is not portable
 * here, and a channel number does not name the same hardware on both parts. */

#endif /* __PWM_X1600_H__ */
