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

#ifndef __IRQ_X1600_H__
#define __IRQ_X1600_H__

/* The flat INTC source numbers (IRQ_AUDIO, IRQ_UART0, ...) live in
 * x1600/intc.h next to the register definitions, because they *are* register
 * bit positions.  This header adds the layer above that: the extension of the
 * numbering to cover per-pin GPIO interrupts, and the group/bit arithmetic the
 * dispatcher in system-x1600.c needs.
 *
 * !! X1600 vs X1000 !!  Do not reuse ingenic_x1000/irq-x1000.h.  Only the
 * four GPIO sources happen to land on the same bits (14..17); everything else
 * moved.  In particular the X1000's IRQ1_I2C(c) = 28 - c formula does not
 * hold: on the X1600 I2C0 is 61 and I2C1 is 60, which is 61 - c, and there is
 * no I2C2 at all.  See the cross-check table in x1600/intc.h.
 */
#include "x1600/intc.h"

/* IRQ_I2C(c) for the two controllers the X1600 has.  Written as a table
 * lookup rather than arithmetic so that a hypothetical third channel cannot
 * silently produce a bogus number. */
#define IRQ_I2C(c)  ((c) == 0 ? IRQ_I2C0 : IRQ_I2C1)

/* GPIO interrupts are demultiplexed in software from the four IRQ_GPIOn
 * sources, and are given numbers above the 64 INTC sources:
 *
 *      IRQ 64 + 32*port + pin,  port 0..3 = PA..PD
 *
 * PA/PB have 32 pins each.  PC has 32 (PC00..PC31; PC31 is KEY_POWER, which
 * is the highest-numbered pin actually used on this board).  PD has 6
 * (PD00..PD05, the microSD/MSC1 group).  Numbers are allocated for a full 32
 * pins per port anyway to keep the arithmetic a shift.
 *
 * NOTE this differs from the X1000, where port C has 26 pins and port D 6.
 * The X1600 PM's port summary tables (21-2 .. 21-5) list PA00-31, PB00-31,
 * PC00-31 and PD00-05.
 */
#define IRQ_GPIO(port, pin) (64 + 32*(port) + (pin))

#define IRQ_GPIO_A_BASE     IRQ_GPIO(0, 0)
#define IRQ_GPIO_D_LAST     IRQ_GPIO(3, 5)
#define IRQ_COUNT           (IRQ_GPIO(3, 6))

/* Group tests/conversions used by system_enable_irq()/system_disable_irq()
 * and by the vectoring code.  Group 0 is INTC sources 0..31, group 1 is
 * 32..63, and anything >= 64 is a GPIO pin interrupt. */
#define IRQ_IS_GROUP0(irq)      (((irq) & 0xffffffe0) == 0x00)
#define IRQ_IS_GROUP1(irq)      (((irq) & 0xffffffe0) == 0x20)
#define IRQ_IS_GPIO(irq)        ((irq) >= 64)

#define IRQ_TO_GROUP0(irq)      (irq)
#define IRQ_TO_GROUP1(irq)      ((irq) - 32)
#define IRQ_TO_GPIO_PORT(irq)   (((irq) - 64) >> 5)
#define IRQ_TO_GPIO_PIN(irq)    (((irq) - 64) & 0x1f)

/* INTC interrupt numbers, and the helper that maps one to a bit.
 *
 * Not register definitions, so not in x1600/ -- those headers are generated
 * from utils/reggen-ng/x1600.reggen.  The X1000 keeps the same constants in
 * its driver headers for the same reason. */
#define IRQ_AUDIO       0   /* AIC */
#define IRQ_USB_OTG     1   /* live */
#define IRQ_PDMA        3   /* live */
#define IRQ_PDMAD       4   /* live: PDMA descriptor */
#define IRQ_PDMAM       5   /* live: PDMA MCU ("xburst-mcu") */
#define IRQ_PWM         6
#define IRQ_SFC         7   /* live */
#define IRQ_SSI0        9
#define IRQ_SADC        11  /* live */
#define IRQ_GPIO3       14
#define IRQ_GPIO2       15
#define IRQ_GPIO1       16
#define IRQ_GPIO0       17
#define IRQ_HASH        22
#define IRQ_AES         23
#define IRQ_TCU2        25
#define IRQ_TCU1        26  /* live */
#define IRQ_TCU0        27  /* live */
#define IRQ_MIPI_CSI    28
#define IRQ_CIM         30
#define IRQ_LCD         31  /* live -- this is the DPU on X1600 */
#define IRQ_RTC         32  /* live */
#define IRQ_SOFT        33  /* CPM software interrupt, see CPM_SFTINT */
#define IRQ_DTRNG       34
#define IRQ_MSC1        36  /* live -- microSD on the HiBy R1 */
#define IRQ_MSC0        37  /* live -- SDIO wifi on the HiBy R1 */
#define IRQ_CAN0        40
#define IRQ_CAN1        41
#define IRQ_CDBUS       42
#define IRQ_UART3       44
#define IRQ_UART2       45  /* live */
#define IRQ_UART1       46
#define IRQ_UART0       47  /* live */
#define IRQ_HARB2       49
#define IRQ_HARB0       50
#define IRQ_CPM         51
#define IRQ_DDR         52
#define IRQ_EFUSE       54
#define IRQ_GMAC        55
#define IRQ_I2C1        60  /* live -- touch panel on the HiBy R1 */
#define IRQ_I2C0        61  /* live -- AXP2101 PMU + CW2015 gauge */
#define IRQ_SSI_SLV     62
#define IRQ_INTC_COUNT  64

#define IRQ_TO_BIT(n)   (1u << ((n) & 31))

#endif /* __IRQ_X1600_H__ */
