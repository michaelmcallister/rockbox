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
 * Ingenic X1600 display controller (DPU / "DC") driver.
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

/* The DPU is a scanout engine: it walks a chain of five-word descriptors in
 * DRAM and streams the named framebuffer to the panel continuously, in step
 * with a TFT timing generator it also contains.  A self-pointing descriptor
 * scans one buffer forever, which is what this driver builds.  So lcd_update()
 * never touches the DPU; it only has to make the new pixels visible in DRAM,
 * copying into a separate scanout buffer as x1000/lcd-x1000.c does.
 *
 * Registers come from x1600/dpu.h (PM chapter 10); the programmed values
 * reproduce a dump from the running vendor kernel.
 *
 * The block is clock-gated and its SRAM powered down when the panel sleeps;
 * gated, every register reads 0x00000080 and a write can hang the bus, so
 * dpu_power(true) must run first and dpu_powered guards the accessors.
 * Powering the RAM back up resets the register file.  Only a few address bits
 * are decoded -- DISP_COM is mirrored across 0x8000..0x807f. */

#include "lcd.h"
#include "system.h"
#include "kernel.h"
#include "dpu-x1600.h"
#include "dma-ingenic.h"
#include "clk-x1600.h"
#include "x1600/dpu.h"
#include "x1600/cpm.h"
#include "x1600/dma.h"
#include "x1600/dma_chn.h"
#include <stdint.h>
#include <string.h>

/* The copy path needs whole scanlines contiguous, which is also the only layout
 * the DPU's Stride descriptor field can describe. */
#if defined(LCD_STRIDEFORMAT) && LCD_STRIDEFORMAT == VERTICAL_STRIDE
# error "dpu-x1600.c: the DPU scans out row-major; vertical stride is unsupported"
#endif

#if LCD_DEPTH != 16
/* FORMAT can also do RGB555 and RGB888; nothing else here handles them. */
# error "dpu-x1600.c: only LCD_DEPTH == 16 (RGB565) is implemented"
#endif

/* Derived TFT timing.  See the conversion table in dpu_set_timings(). */
#define DPU_HTOTAL  (lcd_tgt_config.hsync_len + lcd_tgt_config.left_margin + \
                     LCD_WIDTH + lcd_tgt_config.right_margin)
#define DPU_VTOTAL  (lcd_tgt_config.vsync_len + lcd_tgt_config.upper_margin + \
                     LCD_HEIGHT + lcd_tgt_config.lower_margin)

/* A frame is ~16 ms at this panel's ~61 Hz, so 100 ms means "not responding". */
#define DPU_TIMEOUT_US  100000

/* State */

/* The buffer the DPU scans out of -- NOT Rockbox's framebuffer. */
static fb_data lcd_scanoutfb[LCD_HEIGHT][LCD_WIDTH]
    __attribute__((aligned(64)));

#define LCD_SCANOUTFB_BYTES  (sizeof(lcd_scanoutfb))


/* The SRD descriptor.  PM 10.5.3.1.1 wants 64-bit alignment; a whole cache line
 * keeps it clear of unrelated variables.  Accessed through lcd_srd, an uncached
 * alias: the DPU refetches it every frame and a page flip must be one store. */
static uint32_t lcd_srd_desc[8] __attribute__((aligned(32)));
static volatile uint32_t* lcd_srd;

/* Set once dpu_power(true) has run: until then the register file must not be
 * touched at all. */
static bool dpu_powered = false;

static bool lcd_on = false;

/* lcd_enable(false) left the panel controller asleep, so lcd_enable(true) wakes
 * it rather than running a full init. */
static bool lcd_sleeping = false;

/* Diagnostics only; the copy path resolves FBADDR(0,0) live, as lcd-x1000.c
 * does, because the Rockbox framebuffer moves. */
static fb_data* lcd_fbbase;

static volatile int fbcopy_done;

/* Counted, not latched: "never worked" and "worked once" are different bugs. */
static volatile uint32_t lcd_fbcopy_fails;
static volatile uint32_t lcd_fbcopy_runs;

/* Scanout stops that went unacknowledged.  Harmless from dpu_power(false),
 * which gates the clock anyway; NOT from dpu_start(), which is about to
 * reprogram a DPU that may still be scanning. */
static volatile uint32_t dpu_stop_noack;

/* lcd_set_clock() bailed with a stopped source PLL, so the panel has no dot
 * clock -- indistinguishable from every other cause of a dark screen.
 * lcd_clk_in_freq separates "PLL stopped" (0) from "asked for 0 Hz". */
static volatile uint32_t lcd_clk_unset;
static volatile uint32_t lcd_clk_in_freq;

/* Panic mode: interrupts are off, so the DMA completion interrupt never arrives
 * and the copy must be done in software. */
#define lcd_panic_mode  UNLIKELY((read_c0_status() & 1) == 0)

/* Bounded busy-wait.  Iteration guard as well as an OST deadline: if the OST is
 * not counting the deadline never expires, and the loop would be unbounded. */
#define WAIT_UNTIL(cond, timeout_us)                                        \
    ({                                                                      \
        uint32_t __t0 = __ost_read32();                                     \
        uint32_t __guard = 2000000u;                                        \
        bool __ok;                                                          \
        while(!(__ok = !!(cond)) && --__guard &&                            \
              __ost_read32() - __t0 < (timeout_us) * OST_TICKS_PER_US)      \
            ;                                                               \
        __ok;                                                               \
    })

/* Clock and power */

/* The vendor's parent is MPLL: 1400 / 50 = 28.000 MHz exactly, which EPLL
 * cannot make with an integer divider -- hence R1_LCD_CLKSRC.  Asking for EPLL
 * is the trap: clk_init() leaves both PLLs as found, so if nothing started EPLL
 * then clk_get() returns 0, this bails out, and the panel gets no dot clock. */
void lcd_set_clock(x1600_clk_t clksrc, uint32_t freq)
{
    /* LPCDR's mux is its own three-way encoding (PM 11.1.2.7), not CPCCR's. */
    uint32_t lpcs;
    switch(clksrc) {
    case X1600_CLK_SCLK_A: lpcs = BV_CPM_LPCDR_LPCS__SCLK_A; break;
    case X1600_CLK_MPLL:   lpcs = BV_CPM_LPCDR_LPCS__MPLL;   break;
    case X1600_CLK_EPLL:   lpcs = BV_CPM_LPCDR_LPCS__EPLL;   break;
    default: return;    /* not reachable through this mux */
    }

    uint32_t in_freq = clk_get(clksrc);
    if(in_freq == 0 || freq == 0) {
        /* Leave the clock alone but record it: the one outcome this function
         * warns about is also the one that would otherwise leave no trace. */
        lcd_clk_unset = 1;
        lcd_clk_in_freq = in_freq;
        return;
    }

    /* Ratio is LPCDR + 1, and LPCDR is 8 bits, so 1..256. */
    uint32_t div = clk_calc_div(in_freq, freq);
    if(div < 1 || div > 256)
        return;

    /* PM 11.1.2.7: LPCS "is no glitch free mux.  Software should be stop PIX
     * clock, when change this bit."  Each step arms CE_LCD, which is what makes
     * a write to LPCDR take effect, and waits for LCD_BUSY to fall. */
    jz_writef(CPM_LPCDR, CE_LCD(1), LCD_STOP(1));
    WAIT_UNTIL(!jz_readf(CPM_LPCDR, LCD_BUSY), DPU_TIMEOUT_US);

    jz_writef(CPM_LPCDR, CE_LCD(1), LCD_STOP(0), LPCS(lpcs),
              LCD_IO_INV(0), LPCDR(div - 1));
    WAIT_UNTIL(!jz_readf(CPM_LPCDR, LCD_BUSY), DPU_TIMEOUT_US);

    jz_writef(CPM_LPCDR, CE_LCD(0));
}

/* PM 11.2.2.10: the module clock must not be gated while its SRAM is powered
 * down, so the orderings mirror -- clock on then RAM on, RAM off then clock. */
static void dpu_power(bool on)
{
    if(on == dpu_powered)
        return;

    if(on) {
        jz_writef(CPM_CLKGR, LCD(0));
        jz_writef(CPM_MEMPD1, DPU(0));

        /* Read back rather than assume: everything downstream trusts
         * dpu_powered, and reading a still-gated DPU hangs the bus with no way
         * back.  Both bits are in CPM, which is always accessible.  Each reads
         * 1 while the clock is gated / the SRAM is powered down. */
        if(jz_readf(CPM_CLKGR, LCD) != 0 || jz_readf(CPM_MEMPD1, DPU) != 0) {
            dpu_powered = false;
            return;
        }
        /* TODO(x1600): unverified settling time -- the PM gives no figure for
         * the SRAM power switch chain.  100 us is far longer than plausible. */
        udelay(100);
        dpu_powered = true;
    } else {
        dpu_powered = false;
        jz_writef(CPM_MEMPD1, DPU(1));
        jz_writef(CPM_CLKGR, LCD(1));
    }
}

/* Controller programming */

/* Poll DC_ST until every bit in 'mask' is set.  Returns false on timeout, or
 * immediately if the block is down (reading it then would hang the bus). */
static bool dpu_wait_status(uint32_t mask)
{
    if(!dpu_powered)
        return false;

    return WAIT_UNTIL((REG_DPU_ST & mask) == mask, DPU_TIMEOUT_US);
}

/* Wait for the end of the frame being displayed.  DC_ST's bits are set whether
 * or not the matching interrupt is unmasked, so this works with interrupts off.
 * Returns false on timeout, which normally means scanout is not running. */
static bool dpu_wait_frame(void)
{
    if(!dpu_powered)
        return false;

    jz_writef(DPU_CLR_ST, CLR_DISP_END(1));
    return dpu_wait_status(BM_DPU_ST_DISP_END);
}

/* Program the TFT timing generator and pixel transfer format.  PM 10.5.4.2.1-4:
 * start/end pairs in dot clocks, lines for the vertical pair.  None of the four
 * fields may be zero.  The result matches the live vendor registers. */
static void dpu_set_timings(void)
{
    jz_overwritef(DPU_TFT_TIMING_HSYNC,
                  HPS(lcd_tgt_config.hsync_len),
                  HPE(DPU_HTOTAL));
    jz_overwritef(DPU_TFT_TIMING_VSYNC,
                  VPS(lcd_tgt_config.vsync_len),
                  VPE(DPU_VTOTAL));
    jz_overwritef(DPU_TFT_TIMING_HDE,
                  HDS(lcd_tgt_config.hsync_len + lcd_tgt_config.left_margin),
                  HDE(lcd_tgt_config.hsync_len + lcd_tgt_config.left_margin +
                      LCD_WIDTH));
    jz_overwritef(DPU_TFT_TIMING_VDE,
                  VDS(lcd_tgt_config.vsync_len + lcd_tgt_config.upper_margin),
                  VDE(lcd_tgt_config.vsync_len + lcd_tgt_config.upper_margin +
                      LCD_HEIGHT));

    /* Read from the vendor kernel with the panel working, not derived.  This is
     * SoC-to-panel wiring, not panel timing, hence not in lcd_tgt_config -- a
     * second X1600 board would have to lift these into its own target. */
    jz_overwritef(DPU_TFT_TRAN_CFG,
                  COLOR_EVEN_V(BGR), COLOR_ODD_V(BGR),
                  PIX_CLK_INV(0), DE_DL(0), HSYNC_DL(0), VSYNC_DL(0),
                  MODE_V(PARALLEL_888));

    /* DC_COM_CONFIG and the DC_PCFG_* QoS registers stay at reset: live
     * hardware ran with those values, and DC_COM_CONFIG has undocumented reset
     * bits (see x1600/dpu.h). */
}

/* Build the self-looping SRD descriptor.  Reproduces the vendor's descriptor
 * word for word apart from the framebuffer address. */
static void dpu_init_descriptor(void)
{
    /* Every descriptor write goes through this alias, so DRAM always holds the
     * authoritative copy.  Safe without cache maintenance: uncached stores are
     * coherent with the D-cache on this part, so a later eviction of the cached
     * line writes back the same bytes. */
    lcd_srd = (volatile uint32_t*)UNCACHEDADDR(lcd_srd_desc);

    /* Word 0 points at the descriptor itself, so the DPU refetches it every
     * frame and scans forever.  PM 10.7: a continuous chain needs a frame of at
     * least 500 pixels; ours is LCD_WIDTH*LCD_HEIGHT. */
    lcd_srd[DPU_SRD_NEXT]   = PHYSADDR(lcd_srd_desc);
    lcd_srd[DPU_SRD_FBADDR] = PHYSADDR(lcd_scanoutfb);
    lcd_srd[DPU_SRD_STRIDE] = BF_DPU_SRD_STRIDE_STRIDE(LCD_WIDTH);
    lcd_srd[DPU_SRD_FORMAT] = BF_DPU_SRD_FORMAT_FORMAT(BV_DPU_SRD_FORMAT_FORMAT__RGB565) |
                              BF_DPU_SRD_FORMAT_COLOR(BV_DPU_SRD_FORMAT_COLOR__RGB) |
                              BF_DPU_SRD_FORMAT_CHAIN_END(0);
    /* Identical to the vendor descriptor.  It raises no interrupt (PM 10.6.5: a
     * per-descriptor mask only fires if DC_INTC unmasks the same source, and we
     * leave DC_INTC at zero), and it keeps dpu_wait_frame() working even under
     * the pessimistic reading where the mask also gates DC_ST.DISP_END. */
    lcd_srd[DPU_SRD_INTMASK] = BM_DPU_SRD_INTMASK_EOD_MSK;
}

/* Stop scanout, if running, and stop driving the panel.  PM 10.5.3.2:
 * GEN_STP_SRD finishes the frame in flight; QCK_STP_SRD does not, and would
 * leave the panel half-fed. */
static void dpu_stop(void)
{
    if(!dpu_powered)
        return;

    if(REG_DPU_ST & jz_orm(DPU_ST, WORKING, SRD_WORKING)) {
        jz_writef(DPU_CLR_ST, CLR_STP_SRD_ACK(1));
        jz_writef(DPU_CTRL, GEN_STP_SRD(1));

        /* Non-zero means we reconfigured a DPU that was still scanning. */
        if(!dpu_wait_status(BM_DPU_ST_STOP_SRD_ACK))
            dpu_stop_noack++;
        jz_writef(DPU_CLR_ST, CLR_STP_SRD_ACK(1));
    }

    /* Out of TFT mode, so the panel stops seeing a dot clock.  RMW: DISP_COM's
     * three CLKGATE_EN bits (7:5) must survive. */
    jz_writef(DPU_DISP_COM, DP_IF_SEL_V(NONE));
}

/* Program the controller from scratch and start scanning out.  Called from
 * lcd_init_device() and again from lcd_enable(true), because cycling
 * CPM_MEMPD1.DPU resets the whole register file. */
static void dpu_start(void)
{
    /* Chain-loaded from the vendor OS, the DPU may still be scanning out of the
     * vendor kernel's framebuffer. */
    dpu_stop();

    /* Clear any stale status left by whoever ran before us. */
    jz_writef(DPU_CLR_ST, CLR_DISP_END(1), CLR_TFT_UNDR(1),
              CLR_STP_SRD_ACK(1), CLR_SRD_START(1), CLR_SRD_END(1));

    /* No DPU interrupts: updates are pushed by lcd_update() and scanout never
     * needs re-arming, so IRQ_LCD stays quiet with no handler installed. */
    jz_write(DPU_INTC, 0);

    dpu_set_timings();
    dpu_init_descriptor();

    /* PM 10.6.1: descriptor address, then interface select, then kick.  RMW of
     * DP_DITHER_EN and DP_IF_SEL only, so DISP_COM bits 7:5 keep their power-up
     * value.  Dither off: PM 10.5.4.1 requires it disabled below RGB888. */
    REG_DPU_SRD_CHAIN_ADDR = PHYSADDR(lcd_srd_desc);
    jz_writef(DPU_DISP_COM, DP_DITHER_EN(0), DP_IF_SEL_V(TFT));
    jz_writef(DPU_SRD_CHAIN_CTRL, SRD_CHAIN_START(1));
}

/* Framebuffer copy */

static void lcd_fbcopy_dma_cb(int evt)
{
    (void)evt;
    fbcopy_done = 1;
}

/* Spin until the copy lands.  False on timeout, which means the PDMA channel is
 * wedged; failing rather than hanging keeps the UI alive. */
static bool lcd_fbcopy_wait(void)
{
    /* Either the interrupt flag or the channel's transfer-terminated bit: the
     * DMA can complete with CS.TT set while fbcopy_done stays 0, and CS.TT
     * needs no interrupt delivery -- which matters in the bootloader. */
    return WAIT_UNTIL(fbcopy_done ||
                      jz_readf(DMA_CHN_CS(DMA_CHANNEL_FBCOPY), TT),
                      DPU_TIMEOUT_US);
}

/* Run one PDMA descriptor on the framebuffer-copy channel and wait for it.
 * The register sequence is the X1000 port's, and the X1600 PDMA is
 * register-identical to the X1000's apart from the DRT request-type table
 * (see dma-ingenic.h). */
static void lcd_fbcopy_dma_run(dma_desc* d);
static void lcd_fbcopy_setup(dma_desc* d, int stride_enable);


static void lcd_fbcopy_dma_run(dma_desc* d)
{
    /* Publish the descriptor through the uncached alias: a 16-byte stack
     * descriptor stays hot and dirty in L1 where a 768 KB buffer evicts itself,
     * so the pixels can arrive while the descriptor does not. */
    {
        volatile dma_desc* ud = (volatile dma_desc*)UNCACHEDADDR(d);
        ud->cm = d->cm;
        ud->sa = d->sa;
        ud->ta = d->ta;
        ud->tc = d->tc;
        ud->sd = d->sd;
        ud->rt = d->rt;
        ud->pad0 = d->pad0;
        ud->pad1 = d->pad1;
    }

    fbcopy_done = 0;
    REG_DMA_CHN_DA(DMA_CHANNEL_FBCOPY) = PHYSADDR(d);
    jz_writef(DMA_CHN_CS(DMA_CHANNEL_FBCOPY), DES8(1), NDES(0));
    jz_set(DMA_DB, 1 << DMA_CHANNEL_FBCOPY);
    jz_writef(DMA_CHN_CS(DMA_CHANNEL_FBCOPY), CTE(1));

    lcd_fbcopy_runs++;
    if(!lcd_fbcopy_wait())
        lcd_fbcopy_fails++;
}

/* Common part of the two copy paths.  PM 18.5.6: DDR-to-DDR requires SP and DP
 * both 32-bit.  PM Table 18-6: RDIL 9 is the largest recommended unit.  PM
 * Table 18-5: request type 001000 is auto-request, external to external. */
static void lcd_fbcopy_setup(dma_desc* d, int stride_enable)
{
    d->cm = jz_orf(DMA_CHN_CM, SAI(1), DAI(1), RDIL(9),
                   SP_V(32BIT), DP_V(32BIT), TSZ_V(AUTO),
                   STDE(stride_enable), TIE(1), LINK(0));
    d->rt = jz_orf(DMA_CHN_RT, TYPE_V(AUTO));
    d->sd = 0;
    d->pad0 = 0;
    d->pad1 = 0;
}

/* Copy a run of pixels contiguous in both buffers, i.e. whole scanlines.
 * 'first' and 'count' are in pixels. */
static void lcd_fbcopy_linear(unsigned first, unsigned count)
{
    const fb_data* src = (const fb_data*)FBADDR(0, 0) + first;
    fb_data* dst = &lcd_scanoutfb[0][0] + first;
    unsigned bytes = count * sizeof(fb_data);

    if(lcd_panic_mode) {
        memcpy(dst, src, bytes);
        commit_dcache_range(dst, bytes);
        return;
    }

    /* Publish the SOURCE before handing it to the engine: drawing wrote it
     * through the cache, the engine reads DRAM. */
    commit_dcache_range(src, bytes);
    dma_desc d;
    lcd_fbcopy_setup(&d, 0);
    d.sa = PHYSADDR(src);
    d.ta = PHYSADDR(dst);
    /* With TSZ = AUTO the transfer count is in bytes (PM 18.5.3 note 1), and
     * the field is 24 bits, so up to 16 MiB -- a whole frame is 768000. */
    d.tc = bytes;
    lcd_fbcopy_dma_run(&d);
}

/* Copy a rectangle narrower than the full screen.  'height' must be <= 255,
 * see lcd_fbcopy_rect(). */
static void lcd_fbcopy_rows(int x, int y, int width, int height)
{
    const fb_data* src = (const fb_data*)FBADDR(0, 0) + (y * LCD_WIDTH + x);
    fb_data* dst = &lcd_scanoutfb[y][x];
    unsigned rowbytes = width * sizeof(fb_data);
    unsigned gap = (LCD_WIDTH - width) * sizeof(fb_data);

    if(lcd_panic_mode) {
        for(int i = 0; i < height; ++i) {
            memcpy(dst, src, rowbytes);
            /* EVERY row: a rect has no tail, the whole thing is the tail.  A
             * rect under 16 KiB is never evicted by anything, so without this it
             * can sit in the D-cache indefinitely.  Reached by the scroll engine
             * on every scrolling line, and by splash()/yesno/colour picker. */
            commit_dcache_range(dst, rowbytes);
            src += LCD_WIDTH;
            dst += LCD_WIDTH;
        }

        /* Nothing to do: every row above was published as it was copied. */
        return;
    }

    /* Publish the SOURCE before handing it to the engine.  Strided, so publish
     * the whole span covered -- the last row ends at rowbytes, not a full
     * stride, and under-publishing leaves the engine on a stale final row. */
    commit_dcache_range(src, (height - 1) * LCD_WIDTH * sizeof(fb_data) + rowbytes);
    dma_desc d;
    lcd_fbcopy_setup(&d, 1);
    d.sa = PHYSADDR(src);
    d.ta = PHYSADDR(dst);

    /* Stride mode.  PM 18.5.8: the stride difference works out as exactly the
     * gap in bytes, and source and destination share geometry so SSD == TSD.
     * PM 18.5.3: DTC's low 16 bits are one sub-block's size and the high 8 are
     * the sub-block count -- hence the 255-row cap. */
    d.sd = (gap << 16) | gap;
    d.tc = ((unsigned)height << 16) | rowbytes;

    lcd_fbcopy_dma_run(&d);
}

static void lcd_fbcopy_rect(int x, int y, int width, int height)
{
    /* A full-width rectangle is contiguous in both buffers, so it goes as one
     * linear transfer with no sub-block limit. */
    if(width == LCD_WIDTH) {
        lcd_fbcopy_linear(y * LCD_WIDTH, (unsigned)width * height);
        return;
    }

    do {
        int count = MIN(height, 255);
        lcd_fbcopy_rows(x, y, width, count);
        height -= count;
        y += count;
    } while(height > 0);
}

static void lcd_fbcopy_full(void)
{
    lcd_fbcopy_linear(0, LCD_WIDTH * LCD_HEIGHT);
}

/* Rockbox LCD driver interface */

void lcd_init_device(void)
{
    lcd_fbbase = FBADDR(0, 0);

    /* Start from a known-black scanout buffer: the DPU may read it before the
     * first lcd_update(), and .bss is not guaranteed when chain-loaded.  The
     * publish is what stops it scanning out whatever .bss held. */
    memset(lcd_scanoutfb, 0, LCD_SCANOUTFB_BYTES);
    commit_dcache_range(lcd_scanoutfb, LCD_SCANOUTFB_BYTES);

    dma_set_callback(DMA_CHANNEL_FBCOPY, lcd_fbcopy_dma_cb);

    dpu_power(true);

    /* Panel first, then scanout, mirroring ingenic_x1000/lcd-x1000.c.  Proven
     * on hardware: the ST7701S accepts its init sequence with no dot clock yet.
     * Some RGB panel controllers do not -- if a future panel comes up blank or
     * scrambled, move dpu_set_timings() + DP_IF_SEL = TFT ahead of
     * lcd_tgt_enable(true) and leave only the SRD kick after it. */
    lcd_tgt_enable(true);

    /* Make the first displayed frame the framebuffer Rockbox has already
     * cleared, rather than whatever memset left behind. */
    commit_dcache();
    lcd_fbcopy_full();

    dpu_start();
    lcd_on = true;
}

void lcd_update(void)
{
    if(!lcd_on)
        return;

    /* The DPU reads DRAM directly, so the CPU's writes have to be out of the
     * D-cache before the copy engine sees them. */
    commit_dcache();

    lcd_fbcopy_full();
}

void lcd_update_rect(int x, int y, int width, int height)
{
    if(!lcd_on)
        return;

    /* Clamp, exactly as the other Rockbox drivers do. */
    if(x < 0) {
        width += x;
        x = 0;
    }

    if(y < 0) {
        height += y;
        y = 0;
    }

    if(width > LCD_WIDTH - x)
        width = LCD_WIDTH - x;

    if(height > LCD_HEIGHT - y)
        height = LCD_HEIGHT - y;

    if(width <= 0 || height <= 0)
        return;

    commit_dcache();

    lcd_fbcopy_rect(x, y, width, height);
}

#ifdef HAVE_LCD_ENABLE
bool lcd_active(void)
{
    return lcd_on;
}

void lcd_enable(bool en)
{
    if(en == lcd_on)
        return;

    if(!en) {
        /* Let the frame in flight finish so the panel is not cut off
         * mid-scanline, then shut the controller down. */
        dpu_wait_frame();
        dpu_stop();

        lcd_tgt_sleep(true);
        lcd_sleeping = true;
        lcd_on = false;

        /* Both halves of what the vendor kernel does when the panel sleeps,
         * and what makes every DPU register read back 0x80 afterwards. */
        dpu_power(false);
        return;
    }

    dpu_power(true);

    if(lcd_sleeping) {
        lcd_tgt_sleep(false);
        lcd_sleeping = false;
    } else {
        lcd_tgt_enable(true);
    }

    /* Anyone who wants to repaint on wake hooks this. */
    send_event(LCD_EVENT_ACTIVATION, NULL);

    /* The scanout buffer lives in DRAM and survived the power-down, so the
     * screen comes back showing what it showed before. */
    dpu_start();
    lcd_on = true;
}
#endif /* HAVE_LCD_ENABLE */

#ifdef HAVE_LCD_SLEEP
/* Not enabled in hibyr1native.h yet, but kept so turning it on is a config
 * change.  lcd_enable(false) already drops the DPU power domain; sleep only
 * adds powering the panel down rather than sleeping its controller. */
void lcd_sleep(void)
{
    if(lcd_on)
        lcd_enable(false);

    if(lcd_sleeping) {
        lcd_tgt_enable(false);
        lcd_sleeping = false;
    }
}
#endif /* HAVE_LCD_SLEEP */

#ifdef HAVE_LCD_SHUTDOWN
void lcd_shutdown(void)
{
    if(lcd_on) {
        dpu_wait_frame();
        dpu_stop();
        lcd_on = false;
    }

    lcd_tgt_enable(false);
    lcd_sleeping = false;
    dpu_power(false);
}
#endif /* HAVE_LCD_SHUTDOWN */

/* Debug accessors */

uint32_t dpu_get_scanout_addr(void)
{
    return lcd_srd ? lcd_srd[DPU_SRD_FBADDR] : 0;
}

uint32_t dpu_get_status(void)
{
    /* Reading the block while it is gated or powered down hangs the bus. */
    return dpu_powered ? REG_DPU_ST : 0;
}

void* lcd_fbbase_debug(void) { return lcd_fbbase; }

uint32_t lcd_dma_debug(int which)
{
    switch(which) {
    case 0: return lcd_fbcopy_runs;
    case 1: return lcd_fbcopy_fails;
    case 2: return REG_DMA_CHN_CS(DMA_CHANNEL_FBCOPY);
    /* PDMA clock gated? A gated controller accepts the writes and does
     * nothing, which is exactly what this looks like. */
    case 3: return jz_readf(CPM_CLKGR, PDMA);
    default: return 0;
    }
}

void lcd_update_debug(uint16_t* after_commit, uint16_t* after_copy)
{
    volatile uint16_t* fb = (volatile uint16_t*)FBADDR(0, 0);

    commit_dcache();
    *after_commit = *fb;

    lcd_fbcopy_full();
    *after_copy = *fb;
}

uint32_t dpu_debug_snapshot(int which)
{
    if(!dpu_powered)
        return 0xDEAD0000u | (uint32_t)which;

    switch(which) {
    case 0:  return REG_DPU_ST;
    case 1:  return REG_DPU_DISP_COM;
    case 2:  return REG_DPU_SRD_CHAIN_ADDR;
    case 3:  return REG_DPU_TFT_TIMING_HSYNC;
    case 4:  return REG_DPU_TFT_TIMING_VSYNC;
    case 5:  return REG_DPU_TFT_TIMING_HDE;
    case 6:  return REG_DPU_TFT_TIMING_VDE;
    case 7:  return REG_DPU_TFT_TRAN_CFG;
    case 8:  return lcd_fbcopy_runs;
    case 9:  return lcd_fbcopy_fails;
    case 10: return dpu_stop_noack;
    /* Did the panel ever get a dot clock?  11 == 1 IS the fault. */
    case 11: return lcd_clk_unset;
    case 12: return lcd_clk_in_freq;

    /* Source side.  Against 15 below: src != 0 with dst == 0 is a broken copy
     * path; both zero means nothing was drawn.  14 is guarded because
     * lcd_fbbase is NULL until lcd_init_device() latches it. */
    case 13: return (uint32_t)lcd_fbbase;
    case 14: return lcd_fbbase ? (uint32_t)*(volatile uint16_t*)lcd_fbbase
                               : 0xFBADFBADu;
    /* Destination, read UNCACHED: the DMA allocates no line, so the cached
     * alias can return a stale line from lcd_init_device()'s memset and report
     * a landed copy as missing. */
    case 15: return (uint32_t)*(volatile uint16_t*)
                    UNCACHEDADDR(&lcd_scanoutfb[0][0]);
    /* Does the DPU scan the buffer we copy into?  If the live descriptor points
     * elsewhere it scans memory nobody writes, and 13-15 all look correct. */
    case 16: return lcd_srd ? lcd_srd[DPU_SRD_FBADDR] : 0xFFFFFFFFu;
    case 17: return (uint32_t)PHYSADDR(&lcd_scanoutfb[0][0]);
    default: return 0;
    }
}
