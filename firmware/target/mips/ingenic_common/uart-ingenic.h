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

/* UART interface, shared by the X1000 and the X1600.
 *
 * The data path is common -- the interrupt handler, the FIFO refill/drain and
 * the blocking tx/rx. What differs is setup, and it differs for real reasons:
 * the X1600 has a fourth UART gated from CLKGR1 instead of CLKGR, and its baud
 * rates are searched rather than looked up in a table, because no divider table
 * was ever published for it.
 *
 * uart_port_t and PORT_MAX are defined by the TARGET header, which includes
 * this one afterwards -- the two parts genuinely have a different number of
 * ports, and every array here is sized by it.
 */

#ifndef __UART_INGENIC_H__
#define __UART_INGENIC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Guard on the target headers' own include guards, NOT on PORT_MAX: that is an
 * enum constant, invisible to the preprocessor, so #ifndef PORT_MAX is true in
 * every translation unit. The same mistake was made once with GPIO_PAT0. */
#if !defined(__UART_X1000_H__) && !defined(__UART_X1600_H__)
# error "include uart-x1000.h or uart-x1600.h, not this header directly"
#endif

extern void uart_init(uart_port_t port, int baud);
extern void uart_deinit(uart_port_t port);
extern void uart_tx(uart_port_t port, const uint8_t *buf, size_t len);
extern size_t uart_rx(uart_port_t port, uint8_t *buf, size_t len);
extern bool uart_pending_rx(uart_port_t port);

/* The interrupt handler. Each target defines its own UARTn() vectors -- the
 * names are the SoC's INTC vector slots -- and calls this from them. */
extern void uart_irq(uart_port_t port);

/* ---------------------------------------------------------------------------
 * Supplied by the target (uart-x1000.c / uart-x1600.c)
 */

/* Ungate the port and unmask its INTC source, or the reverse. Panics on a
 * port this SoC does not have. */
extern void uart_soc_enable(uart_port_t port, bool on);

/* Program the divider for `baud`; returns the rate actually achieved, or 0 if
 * this SoC cannot reach it closely enough. uart_init() panics on 0. */
extern int uart_set_baud(uart_port_t port, int baud);

#endif /* __UART_INGENIC_H__ */
