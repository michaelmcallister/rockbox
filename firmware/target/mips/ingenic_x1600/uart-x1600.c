/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 *
 * Based on firmware/target/mips/ingenic_x1000/uart-x1000.c,
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

/* SoC glue for the shared UART driver (ingenic/uart-ingenic.c). */

#include "panic.h"
#include "system.h"
#include "ingenic-soc.h"
#include "uart-baud-x1600.h"

static const uint8_t uart_irq_num[PORT_MAX] = {
    IRQ_UART0, IRQ_UART1, IRQ_UART2, IRQ_UART3
};

void uart_soc_enable(uart_port_t port, bool on)
{
    /* Unlike the X1000, UART3's clock gate is in CLKGR1; UART0..2 stay in
     * CLKGR. `gate` is the inverse of `on`: the register bit gates the clock. */
    int gate = on ? 0 : 1;

    if(on) {
        switch(port) {
        case PORT_UART0: jz_writef(CPM_CLKGR,  UART0(gate)); break;
        case PORT_UART1: jz_writef(CPM_CLKGR,  UART1(gate)); break;
        case PORT_UART2: jz_writef(CPM_CLKGR,  UART2(gate)); break;
        case PORT_UART3: jz_writef(CPM_CLKGR1, UART3(gate)); break;
        default: panicf("invalid UART port %d", port);
        }
        system_enable_irq(uart_irq_num[port]);
    } else {
        system_disable_irq(uart_irq_num[port]);
        switch(port) {
        case PORT_UART0: jz_writef(CPM_CLKGR,  UART0(gate)); break;
        case PORT_UART1: jz_writef(CPM_CLKGR,  UART1(gate)); break;
        case PORT_UART2: jz_writef(CPM_CLKGR,  UART2(gate)); break;
        case PORT_UART3: jz_writef(CPM_CLKGR1, UART3(gate)); break;
        default: panicf("invalid UART port %d", port);
        }
    }
}

/* The (M, divisor) search lives in uart-baud-x1600.h, register-free, so
 * uart-baud-test.c can compile the real thing. See the note there.
 *
 * The device clock is inferred to be EXCLK, not measured: the CPM register map
 * has no UART divider at all. Confirm against a known baud rate once a serial
 * line is physically accessible. */
int uart_set_baud(uart_port_t port, int baud) {
    if(port >= PORT_MAX || baud <= 0)
        return 0;

    uint32_t best_m, best_dl, best_rate, best_err;
    uart_calc_divisor(X1600_EXCLK_FREQ, baud,
                      &best_m, &best_dl, &best_rate, &best_err);

    if(best_m == 0)
        return 0;

    /* Framing tolerates roughly 2-3% clock error across both ends */
    if(best_err * 50 > (uint32_t)baud)
        return 0;

    jz_writef(UART_ULCR(port), DLAB(1));
    jz_write(UART_UDLLR(port), best_dl & 0xff);
    jz_write(UART_UDLHR(port), (best_dl >> 8) & 0xff);
    jz_writef(UART_ULCR(port), DLAB(0));
    jz_write(UART_UMR(port), best_m);
    jz_write(UART_UACR(port), 0);

    return best_rate;
}

/* INTC vector slots. Weak aliases in system-x1600.c point at UIRQ until these
 * definitions take them over. */
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

void UART3(void)
{
    uart_irq(PORT_UART3);
}
