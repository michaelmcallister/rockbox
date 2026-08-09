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

#include "system.h"
#include <limits.h>
#include "usb.h"
#include "usb_core.h"
#include "usb_drv.h"
#include "usb-designware.h"
#include "usb-x1600.h"
#include "panic.h"
#include "irq-x1600.h"
#include "gpio-x1600.h"
#include "axp-2101.h"
#include "x1600/cpm.h"

/* USB-Designware driver API */

const struct usb_dw_config usb_dw_config = {
    .phytype = DWC_PHYTYPE_UTMI_16,

    /* GHWCFG2/3 report 9 endpoints and 3576 words of FIFO, same as the X1000,
     * so the X1000's split applies unchanged */
    .rx_fifosz   = 816, /* shared RxFIFO */
    .nptx_fifosz = 32,  /* only used for EP0 IN */
    .ptx_fifosz  = 384, /* room for 7 IN EPs */

    /* Slave mode, chosen when the port had no working commit_dcache_range().
     * TODO(x1600): both preconditions for revisiting DMA mode are now met --
     * the two-stage cache publish works, and USB enumerates and exports mass
     * storage from a flashed boot. Slave mode makes the CPU copy every byte,
     * so this is a throughput question with nothing blocking it. */
    .disable_double_buffering = false,
};

/* Bring the PHY up, reproducing the state the BootROM leaves it in.
 *
 * Read back from the block while the BootROM had USB working:
 *
 *     USBPCR   0x00000000     (reset is 0x409919b8 -- the BootROM zeroes it)
 *     USBPCR1  0x80000000     BVLD_REG only
 *     USBRDT   0x02000096     reset value, untouched
 *     USBVBFIL 0x00ff0080     reset value, untouched
 *     GHWCFG2  0x228be050     NUMDEVEPS 8, +1 = 9 endpoints, as x1600.h assumes
 *
 * ⚠ DO NOT copy usb-x1000.c's configuration here, however similar the register
 * map looks. The X1000 writes a full tune word into USBPCR and sets
 * REFCLK_SEL = CLKCORE, REFCLK_DIV = 24MHZ, WORD_IF = 16BIT in USBPCR1. On this
 * part the working configuration is the opposite: USBPCR zeroed, and USBPCR1
 * left at CRYSTAL / 12MHZ / 8BIT with only BVLD_REG raised. An earlier revision
 * of this function did copy the X1000 and would have moved the PHY away from
 * the only configuration known to work here.
 *
 * The registers do sit at identical offsets on both parts and USBRDT's reset
 * value carries the same RDT = 0x96 the X1000 programs, so the field map is
 * shared -- but the values that map should hold are not.
 *
 * Why this function has to do anything at all: the comment it replaced said the
 * BootROM brings the PHY up "and this port is only entered from it", true while
 * every boot was a RAM upload and false once the device booted from flash. On a
 * flashed boot nothing has configured the PHY, and usb_dw_check_hw() read a
 * nonsense endpoint count out of GHWCFG2 and panicked. With this, a cold
 * flashed boot enumerates and exports mass storage with no host involved.
 *
 * There is no USBCDR here; the X1600 has no USB clock divider register. */
void usb_dw_target_enable_clocks(void)
{
    jz_writef(CPM_CLKGR, OTG(0));

    /* PHY soft reset -- a level, not a pulse (PM 11.2.2.9) */
    jz_writef(CPM_SRBC, OTG_SR(1));
    udelay(10);
    jz_writef(CPM_SRBC, OTG_SR(0));

    jz_writef(CPM_OPCR, GATE_USBPHY_CLK(0));

    /* Exit suspend */
    jz_writef(CPM_OPCR, SPENDN0(1));
    udelay(45);

    /* The measured working words, written whole rather than field by field:
     * the point is to land exactly where the BootROM does, and a field-wise
     * write would silently preserve whatever the reset left in the gaps. */
    REG_CPM_USBPCR  = 0x00000000;
    REG_CPM_USBPCR1 = 0x80000000;

    /* Power on reset */
    jz_writef(CPM_USBPCR, POR(1));
    mdelay(1);
    jz_writef(CPM_USBPCR, POR(0));
    mdelay(1);
}

void usb_dw_target_disable_clocks(void)
{
    jz_writef(CPM_OPCR, SPENDN0(0));
    udelay(5);
    jz_writef(CPM_OPCR, GATE_USBPHY_CLK(1));
    jz_writef(CPM_CLKGR, OTG(1));
}

void usb_dw_target_enable_irq(void)
{
    system_enable_irq(IRQ_USB_OTG);
}

void usb_dw_target_disable_irq(void)
{
    system_disable_irq(IRQ_USB_OTG);
}

void usb_dw_target_clear_irq(void)
{
}

/* Rockbox API */

/* This board has no USB detect pin, so presence comes from the PMU over I2C.
 * usb_detect() is called from contexts that cannot do I2C, so the reading is
 * cached and refreshed by x1600_usb_refresh_detect(). It uses the VBUS
 * voltage, not AXP2101_INPUT_USB, which reports USB present with VBUS at 0 mV
 * after a disconnect; the voltage needs hysteresis as it ramps. */
#define VBUS_INSERT_MV  4000
#define VBUS_EXTRACT_MV 1500

static volatile int usb_status = USB_EXTRACTED;

/* Thread context only -- does I2C */
void x1600_usb_refresh_detect(void)
{
    int mv = axp2101_adc_read(AXP2101_ADC_VBUS_VOLTAGE);
    int status = usb_status;

    /* A failed I2C read returns INT_MIN, which is below VBUS_EXTRACT_MV and
     * would be reported as a disconnect on a cable that never moved. The PMU
     * does NAK occasionally here, and each spurious event wakes the screen --
     * observed on hardware as a periodic wake with no other effect.
     * Leave the cached status alone and wait for the next sample. */
    if(mv == INT_MIN)
        return;

    if(mv > VBUS_INSERT_MV)
        status = USB_INSERTED;
    else if(mv < VBUS_EXTRACT_MV)
        status = USB_EXTRACTED;

    if(status != usb_status) {
        usb_status = status;
        /* USB_STATUS_BY_EVENT is set, so nothing polls usb_detect() */
        usb_status_event(status);
    }
}

int usb_detect(void)
{
    return usb_status;
}

void usb_enable(bool on)
{
    if(on)
        usb_core_init();
    else
        usb_core_exit();
}

void usb_init_device(void)
{
    /* drvvbus is only used when acting as a host, which Rockbox does not do */
    gpio_set_function(GPIO_USB_DRVVBUS, GPIOF_OUTPUT(0));

    usb_dw_target_disable_irq();
    usb_dw_target_enable_clocks();

    usb_drv_exit();

    /* i2c_init() and power_init() both run before usb_init() */
    x1600_usb_refresh_detect();

#ifdef USB_ENABLE_SERIAL
    /* A console alongside mass storage, and in charge-only mode where the UI
     * stays usable. usb_set_serial() rather than usb_core_enable_driver(): the
     * latter sets a flag usb_core_init() clears on every connect. */
    usb_set_serial(true);
#endif
}
