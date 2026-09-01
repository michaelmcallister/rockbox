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

#ifndef __DPU_X1600_H__
#define __DPU_X1600_H__

/* Needed by the declarations below, not just by the ones further down: the
 * usbstage payloads include this header without pulling these in first. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Whether lcd_update() uses the PDMA or a CPU memcpy.
 *
 * Lives here rather than in dpu-x1600.c because other files legitimately need
 * to know: debug-x1600.c only drives the fbcopy DMA channel for its own test
 * when the display is NOT using it. While this was private to dpu-x1600.c that
 * check silently compiled out, and the debug screen reported "n/a (fbcopy owns
 * chan)" -- a plausible-looking answer to a test that never ran. */



/* Public interface of the X1600 display controller driver -- not lcd-x1600.h.
 * The display block here is a DPU driving a parallel RGB panel from a chain of
 * DMA descriptors in DRAM, completely different IP from the X1000's smart-LCD
 * LCDC, so nothing in ingenic_x1000/lcd-x1000.[ch] applies.
 *
 * dpu-x1600.c owns the controller: gating, TFT timing, the SRD descriptor
 * chain and the Rockbox lcd_*() entry points. <target>/lcd-<target>.c owns the
 * panel -- reset sequencing and its config bus -- and must supply
 * lcd_tgt_config, lcd_tgt_enable() and lcd_tgt_sleep(). */

#include "clk-x1600.h"

/* Panel timing in the fb_videomode idiom, which is how these were captured
 * from the device and how the vendor panel modules express them; dpu-x1600.c
 * converts to the DPU's start/end form. The active area is always
 * LCD_WIDTH x LCD_HEIGHT.
 *
 * Not here: pixel format, sync polarities and RGB lane order. Those are
 * SoC-to-panel wiring, set in dpu-x1600.c from registers read off the running
 * device. Move them here if a second X1600 target ever needs to differ. */
struct lcd_tgt_config {
    /* Pixel (dot) clock in Hz.  This is what the driver *asks* for; the
     * achievable rate is (source PLL / n) for an integer n in 1..256, so the
     * real dot clock may be a few percent lower.  See lcd_set_clock(). */
    uint32_t pixclock;

    /* Horizontal timing, in dot clocks. */
    uint16_t hsync_len;         /* HSYNC pulse width          */
    uint16_t left_margin;       /* horizontal back porch      */
    uint16_t right_margin;      /* horizontal front porch     */

    /* Vertical timing, in lines. */
    uint16_t vsync_len;         /* VSYNC pulse width          */
    uint16_t upper_margin;      /* vertical back porch        */
    uint16_t lower_margin;      /* vertical front porch       */
};

/* Static configuration for the target's panel; defined by the target. */
extern const struct lcd_tgt_config lcd_tgt_config;

/* Program the DPU pixel clock (CPM_LPCDR).
 *
 * clksrc must be one of X1600_CLK_SCLK_A, X1600_CLK_MPLL or X1600_CLK_EPLL --
 * those are the only three inputs the LPCDR mux has (PM 11.1.2.7).  If the
 * chosen source is stopped, or the request cannot be met with the 8-bit
 * divider, the call does nothing and the pixel clock is left as it was; the
 * symptom is a dark or badly-timed panel rather than a hang.
 *
 * Called by the target's lcd_tgt_enable() before it talks to the panel.
 */
extern void lcd_set_clock(x1600_clk_t clksrc, uint32_t freq);

/* Power the panel up or down.  On enable the target must reset the panel, call
 * lcd_set_clock(), and push whatever init sequence the panel controller needs.
 * On disable it should do the reverse, or as much of it as is known safe.
 *
 * Implemented by the target.
 */
extern void lcd_tgt_enable(bool on);

/* Put the panel into or out of its low power state, normally with the
 * controller's own sleep-in/sleep-out commands.  Implemented by the target.
 */
extern void lcd_tgt_sleep(bool sleep);

/* ------------------------------------------------------------------------
 * Below here is internal to the SoC display driver, and is exposed only so a
 * debug screen can look at it.  Nothing else should call these.
 * ------------------------------------------------------------------------ */

/* Physical address the DPU is currently scanning out of.  Zero before
 * lcd_init_device() has run. */
extern uint32_t dpu_get_scanout_addr(void);

/* Raw DC_ST.  Reading it while the block is clock-gated or its RAM is powered
 * down will hang the bus, so this returns 0 unless the driver knows the block
 * is up. */
extern uint32_t dpu_get_status(void);


/* Read back DPU state for bring-up diagnostics.
 *
 *   0 DC_ST   1 DISP_COM   2 SRD_CHAIN_ADDR
 *   3 TIMING_HSYNC  4 TIMING_VSYNC  5 TIMING_HDE  6 TIMING_VDE  7 TFT_TRAN_CFG
 *   8 fbcopy runs    9 fbcopy timeouts  10 dpu_stop non-acks
 *   (8-10 are software counters and need no hardware access)
 *
 * Returns 0xDEAD0000|which if the driver believes the block is unpowered.
 *
 * CAUTION: that guard tests the driver's own dpu_powered flag, which
 * dpu_power() sets without verifying anything. It can therefore be true while
 * the block is still gated, and READING A GATED BLOCK HANGS THE BUS with no way
 * back. Callers that cannot afford a wedge should ask CPM instead:
 *     if(jz_readf(CPM_CLKGR, LCD) || jz_readf(CPM_MEMPD1, DPU)) return;
 * which is the form debug-x1600.c uses and debug-gate-test.py checks for.
 */
uint32_t dpu_debug_snapshot(int which);

/* The framebuffer base lcd_update() actually copies from, latched at init.
 * Exposed so a bring-up build can compare it against a live FBADDR(0,0). */
void* lcd_fbbase_debug(void);


/* Bring-up only: run lcd_update()'s two steps with a sample between them. */
void lcd_update_debug(uint16_t* after_commit, uint16_t* after_copy);

/* Bring-up only: 0=fbcopy runs, 1=fbcopy timeouts, 2=DMA_CHN_CS, 3=CLKGR.PDMA */
uint32_t lcd_dma_debug(int which);

/* SRD descriptor words -- five words in DRAM, not a register block.
 *
 * Not register definitions, so not in x1600/ -- those headers are generated
 * from utils/reggen-ng/x1600.reggen.  The X1000 keeps the same constants in
 * its driver headers for the same reason. */
#define DPU_SRD_DESC_WORDS  5
#define DPU_SRD_NEXT        0
#define DPU_SRD_FBADDR      1
#define DPU_SRD_STRIDE      2
#define BP_DPU_SRD_STRIDE_STRIDE    0
#define BM_DPU_SRD_STRIDE_STRIDE    0x1fff
#define DPU_SRD_FORMAT      3
#define BP_DPU_SRD_FORMAT_FORMAT        19
#define BM_DPU_SRD_FORMAT_FORMAT        0x780000
#define BV_DPU_SRD_FORMAT_FORMAT__RGB555 0x0
#define BV_DPU_SRD_FORMAT_FORMAT__RGB565 0x2
#define BV_DPU_SRD_FORMAT_FORMAT__RGB888 0x4
#define BP_DPU_SRD_FORMAT_COLOR         16
#define BM_DPU_SRD_FORMAT_COLOR         0x70000
#define BV_DPU_SRD_FORMAT_COLOR__RGB    0x0
#define BV_DPU_SRD_FORMAT_COLOR__RBG    0x1
#define BV_DPU_SRD_FORMAT_COLOR__GRB    0x2
#define BV_DPU_SRD_FORMAT_COLOR__GBR    0x3
#define BV_DPU_SRD_FORMAT_COLOR__BRG    0x4
#define BV_DPU_SRD_FORMAT_COLOR__BGR    0x5
#define BP_DPU_SRD_FORMAT_CHAIN_END     0
#define BM_DPU_SRD_FORMAT_CHAIN_END     0x1
#define DPU_SRD_INTMASK     4
#define BM_DPU_SRD_INTMASK_EOD_MSK      0x20000     /* DISP_END   */
#define BM_DPU_SRD_INTMASK_SOS_MSK      0x4         /* SRD_START  */
#define BM_DPU_SRD_INTMASK_EOS_MSK      0x2         /* SRD_END    */

#define BF_DPU_SRD_STRIDE_STRIDE(v) (((v) & 0x1fff) << 0)
#define BF_DPU_SRD_FORMAT_FORMAT(v)     (((v) & 0xf) << 19)
#define BF_DPU_SRD_FORMAT_COLOR(v)      (((v) & 0x7) << 16)
#define BF_DPU_SRD_FORMAT_CHAIN_END(v)  (((v) & 0x1) << 0)

#endif /* __DPU_X1600_H__ */