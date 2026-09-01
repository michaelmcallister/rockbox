/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 *
 * Based on firmware/target/mips/ingenic_x1000/uart-x1000.h,
 * Copyright (C) 2026 Skye Green
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

#ifndef __UART_X1600_H__
#define __UART_X1600_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Four UARTs, and UART3 is gated by CLKGR1 rather than CLKGR. The vendor
 * instantiates only UART0 and UART2, neither broken out on the case. */
typedef enum {
    PORT_UART0 = 0,
    PORT_UART1 = 1,
    PORT_UART2 = 2,
    PORT_UART3 = 3,
    PORT_MAX = 4,
} uart_port_t;

/* Register definitions first, then the shared contract. */
#include "x1600/uart.h"
#include "uart-ingenic.h"

#endif /* __UART_X1600_H__ */
