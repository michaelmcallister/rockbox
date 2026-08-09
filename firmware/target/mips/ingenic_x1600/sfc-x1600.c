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
 * Based on firmware/target/mips/ingenic_x1000/sfc-x1000.c,
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

/* PIO ("slave mode") only; DMA, descriptor chain and CDT mode are not done */

#include "system.h"
#include "spin-x1600.h"
#include "kernel.h"
#include "sfc-x1600.h"
#include "clk-x1600.h"
#include "gpio-x1600.h"
#include "x1600/cpm.h"

/* A quarter of the 128-word FIFO, per the vendor SPL, not the X1000's 31 */
#define SFC_FIFO_THRESH 32

static void sfc_poll_wait(void);

void sfc_configure_pins(void)
{
    /* NOT PA26-31, where the X1000 puts the SFC and this board the display */
    gpioz_configure(GPIO_C, 0x3f << 17, GPIOF_DEVICE(0));
}

void sfc_open(void)
{
    /* Must come first: the boot ROM re-gates SFC, and a gated read hangs */
    jz_writef(CPM_CLKGR, SFC(0));

    /* Named in full so DES_EN and CDT_EN are off whatever the boot ROM left */
    jz_writef(SFC_GLB,
              DES_EN(0),
              CDT_EN(0),
              TRAN_DIR_V(READ),
              THRESHOLD(SFC_FIFO_THRESH),
              OP_MODE_V(SLAVE),
              PHASE_NUM(1),
              WP_EN(1),
              BURST_MD_V(INCR32));

    /* Chip select, already 0. Read-modify-write: the PM marks bit 2
     * reserved-but-set while the register map calls it unwritable. */
    REG_SFC_GLB1 = REG_SFC_GLB1 &
                   ~(SFC_GLB1_CHIP1_SEL_BM | SFC_GLB1_CHIP_SEL_BM);

    /* TRAN_CFG1_0 is left alone; sfc_exec() writes only TRAN_MD */

    REG_SFC_CGE = 0;            /* all internal clock gates open */
    REG_SFC_INTC = 0x1f;        /* mask every interrupt (reset value) */
    REG_SFC_MEM_ADDR = 0;       /* unused in slave mode */

    /* If the controller ever fails to respond, check CPM_MEMPD0.SFC */
}

void sfc_close(void)
{
    REG_SFC_CGE = 0x1f;
    jz_writef(CPM_CLKGR, SFC(1));
}

void sfc_irq_begin(void)
{
    /* Not implemented: needs DMA mode and an IRQ_SFC number */
}

void sfc_irq_end(void)
{
}

void sfc_set_clock(uint32_t freq)
{
    /* FIXME: Get rid of this hack & allow defining a real clock tree... */
    uint32_t sfcs = BV_CPM_SFCCDR_SFCS__MPLL;
    uint32_t in_freq = clk_get(X1600_CLK_MPLL);
    if(in_freq < freq) {
        sfcs = BV_CPM_SFCCDR_SFCS__SCLK_A;
        in_freq = clk_get(X1600_CLK_SCLK_A);
    }

    uint32_t div = clk_calc_div(in_freq, freq);
    if(div < 1)
        div = 1;        /* in_freq == 0: don't program a divider of 0-1 */
    if(div > 256)
        div = 256;      /* SFCCDR is 8 bits, ratio = SFCCDR + 1 */

    jz_writef(CPM_SFCCDR, CE_SFC(1), SFCCDR(div - 1), SFCS(sfcs));
    (void)x1600_spin_while(jz_readf(CPM_SFCCDR, SFC_BUSY));
    jz_writef(CPM_SFCCDR, CE_SFC(0));
}

static void sfc_fifo_rdwr(bool write, void* buffer, uint32_t data_bytes)
{
    uint32_t* word_buf = (uint32_t*)buffer;
    uint32_t sr_bit = write ? BM_SFC_SR_TREQ : BM_SFC_SR_RREQ;
    uint32_t clr_bit = write ? BM_SFC_SCR_CLR_TREQ : BM_SFC_SCR_CLR_RREQ;
    uint32_t data_words = (data_bytes + 3) / 4;
    /* Bounded: a stuck TREQ/RREQ would otherwise spin forever on every boot */
    uint32_t guard = X1600_SPIN_GUARD;
    while(data_words > 0 && --guard) {
        if(REG_SFC_SR & sr_bit) {
            REG_SFC_SCR = clr_bit;

            /* The PM requires each AHB burst to match the THRESHOLD */
            uint32_t amount = MIN(data_words, (uint32_t)SFC_FIFO_THRESH);
            data_words -= amount;

            uint32_t* endptr = word_buf + amount;
            for(; word_buf != endptr; ++word_buf) {
                if(write)
                    REG_SFC_DATA = *word_buf;
                else
                    *word_buf = REG_SFC_DATA;
            }
        }
    }
}

void sfc_exec(uint32_t cmd, uint32_t addr, void* data, uint32_t size)
{
    bool write = (size & SFC_WRITE) != 0;
    size &= ~SFC_WRITE;

    /* Stop anything still in flight, so an aborted previous flow is harmless */
    jz_overwritef(SFC_TRIG, STOP(1));

    /* Deal with transfer direction -- it lives in GLB0, not the command */
    uint32_t glb = REG_SFC_GLB;
    if(data) {
        if(write)
            jz_vwritef(glb, SFC_GLB, TRAN_DIR_V(WRITE));
        else
            jz_vwritef(glb, SFC_GLB, TRAN_DIR_V(READ));
    }
    REG_SFC_GLB = glb;

    /* Transfer config; the line-count mode lives in TRAN_CFG1_0 here */
    uint32_t cfg1 = REG_SFC_TRAN_CONF1(0);
    cfg1 &= ~SFC_TRAN_CONF1_TRAN_MD_BM;
    cfg1 |= SFC_TRAN_CONF1_TRAN_MD(SFC_CMD_GET_TMODE(cmd));
    REG_SFC_TRAN_CONF1(0) = cfg1;

    REG_SFC_TRAN_CONF(0) = cmd & ~SFC_CMD_TMODE_BM;
    REG_SFC_TRAN_LENGTH = size;
    REG_SFC_DEV_ADDR(0) = addr;
    REG_SFC_DEV_PLUS(0) = 0;

    /* Clear old interrupts; we poll, so keep END masked */
    REG_SFC_SCR = 0x1f;
    jz_writef(SFC_INTC, MSK_END(1));

    /* Start the command; every data phase must be preceded by a flush */
    jz_overwritef(SFC_TRIG, FLUSH(1));
    jz_overwritef(SFC_TRIG, START(1));

    if(data)
        sfc_fifo_rdwr(write, data, size);
    sfc_poll_wait();
}

static void sfc_poll_wait(void)
{
    /* Bounded: this links into the SPL, where a spin hangs every boot */
    (void)x1600_spin_until(jz_readf(SFC_SR, END) != 0);
    jz_overwritef(SFC_SCR, CLR_END(1));
}
