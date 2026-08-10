/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
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

/* SoC glue for the shared UART driver (ingenic_common/uart-ingenic.c). */

#include "panic.h"
#include "system.h"
#include "ingenic-soc.h"

void uart_soc_enable(uart_port_t port, bool on)
{
    int gate = on ? 0 : 1;

    if(!on) {
        switch (port) {
            case PORT_UART0: system_disable_irq(IRQ_UART0); break;
            case PORT_UART1: system_disable_irq(IRQ_UART1); break;
            case PORT_UART2: system_disable_irq(IRQ_UART2); break;
            default: panicf("invalid UART port %d", port);
        }
    }

    switch (port) {
        case PORT_UART0: jz_writef(CPM_CLKGR, UART0(gate)); break;
        case PORT_UART1: jz_writef(CPM_CLKGR, UART1(gate)); break;
        case PORT_UART2: jz_writef(CPM_CLKGR, UART2(gate)); break;
        default: panicf("invalid UART port %d", port);
    }

    if(on) {
        switch (port) {
            case PORT_UART0: system_enable_irq(IRQ_UART0); break;
            case PORT_UART1: system_enable_irq(IRQ_UART1); break;
            case PORT_UART2: system_enable_irq(IRQ_UART2); break;
            default: panicf("invalid UART port %d", port);
        }
    }
}

/* A table, not a search: these are the divider values published for the part.
 * Returns the requested rate on success, 0 if it is not in the table --
 * uart_init() turns that into the panic this used to raise itself. */
int uart_set_baud(uart_port_t port, int baud) {
    uint32_t udllr, udlhr, umr, uacr;
    switch (baud) {
#if X1000_EXCLK_FREQ == 24000000
        case 115200:
            udllr = 13;
            udlhr = 0;
            umr = 16;
            uacr = 0;
            break;
        case 1500000:
            udllr = 1;
            udlhr = 0;
            umr = 16;
            uacr = 0;
            break;
        case 2000000:
            udllr = 1;
            udlhr = 0;
            umr = 12;
            uacr = 0;
            break;
        case 3000000:
            udllr = 1;
            udlhr = 0;
            umr = 8;
            uacr = 0;
            break;
        case 4000000:
            udllr = 1;
            udlhr = 0;
            umr = 6;
            uacr = 0;
            break;
#endif
        default:
            return 0;
    }

    jz_writef(UART_ULCR(port), DLAB(1));
    jz_write(UART_UDLLR(port), udllr);
    jz_write(UART_UDLHR(port), udlhr);
    jz_writef(UART_ULCR(port), DLAB(0));
    jz_write(UART_UMR(port), umr);
    jz_write(UART_UACR(port), uacr);

    return baud;
}

void UART0(void)
{
    uart_irq(PORT_UART0);
}

void UART1(void)
{
    uart_irq(PORT_UART1);
}

void UART2(void)
{
    uart_irq(PORT_UART2);
}
