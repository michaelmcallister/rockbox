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
 * Based on firmware/target/mips/ingenic_x1000/debug-x1000.c,
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

/* X1600 debug menu. Every screen here is read-only, and every screen that
 * reads a peripheral checks its clock gate first: reading a gated or reserved
 * register hangs the bus -- not a catchable bus error, the device stops and
 * needs a physical power cycle. Most blocks are gated at BootROM exit and
 * Rockbox only ungates what it drives.
 *
 * So gate-check anything added here. CPM and GPIO are the only two blocks
 * always safe to read. */

#ifndef BOOTLOADER

/* Without this every logf() in the file compiles to nothing, silently --
 * logf.h #undefs it unless the file opts in. All eight calls here were dead,
 * including the ones documented as tee'ing probe results to the console. */
#define LOGF_ENABLE

#include "system.h"
#include "debug-ingenic.h"
#include "kernel.h"
#include "button.h"
#include "lcd.h"
#include "font.h"
#include "logf.h"
#include "action.h"
#include "list.h"
#include "adc.h"
#include "storage.h"
#include "sdmmc.h"
#include "sd.h"

#include "clk-x1600.h"
#include "gpio-x1600.h"
#include "dma-ingenic.h"
#include "dpu-x1600.h"
#include "x1600/dma.h"
#include "x1600/cpm.h"
#include "x1600/aic.h"
#include "x1600/sfc.h"
#include "x1600/dpu.h"
/* For the watchdog probe. Both blocks are always accessible: WDT lives inside
 * TCU, which the tick already drives, so neither needs a gate check. */
#include "x1600/wdt.h"
#include "x1600/tcu.h"
#include "adc-target.h"
#include "rbversion.h"    /* RBVERSION, for the RoLo tag screen */
#ifdef HAVE_CS43131
#include "cs43131.h"
#endif

/* Published by pcm-x1600.c; see debug-x1000.c:119 for the same pattern. */
extern volatile unsigned aic_tx_underruns;

/* ------------------------------------------------------------------------
 * Cache mode
 * ------------------------------------------------------------------------ */

/* Which image was loaded, and what the silicon is doing with KSEG0 */

static bool dbg_cache_mode(void)
{
    do {
        lcd_clear_display();
        lcd_putsf(0, 0, "version   %s", RBVERSION);

        /* Which cache mode is actually in force, read from the hardware rather
         * than inferred from the build flag. CP0 Config K0 (bits 2:0) is the
         * KSEG0 cacheability attribute; 2 = uncached. CP0 is never gated, so
         * this read is always safe.
         *
         * Read from CP0 rather than reported from a build flag, so it says what
         * the silicon did rather than what the build intended -- an ignored
         * mtc0 would otherwise go unnoticed. */
        {
            uint32_t cfg;
            __asm__ __volatile__("mfc0 %0, $16, 0" : "=r"(cfg));
            unsigned k0 = cfg & 7;
            lcd_putsf(0, 1, "KSEG0     %s (Config.K0=%u)",
                      k0 == 2 ? "UNCACHED" : "cached", k0);
        }

        lcd_putsf(0, 3, "version = which image was LOADED");
        lcd_putsf(0, 4, "K0      = what the silicon is doing");
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

/* ------------------------------------------------------------------------
 * Clocks
 * ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------
 * Stacks
 * ------------------------------------------------------------------------ */

/* High-water marks for both stacks by canary scan -- crt0.S fills both with
 * 0xDEADBEEF, so the deepest point reached is the first word that is not,
 * as upstream's debug-jz4760.c does.
 *
 * Worth measuring because static analysis errs in both directions here: it
 * cannot see indirect calls, and it chains paths that cannot co-occur.
 *
 * Neither stack has a guard page, and the IRQ stack grows down out of
 * _irqstackbegin, which IS stackend -- an IRQ overflow silently corrupts the
 * main thread's outermost frames. If "IRQ" reads close to its size, that
 * explains a wedge somewhere unrelated.
 *
 * Pure memory reads, so this one cannot hang the bus. */
static bool dbg_stacks(void)
{
    extern uint32_t stackbegin[], stackend[];
    extern uint32_t _irqstackbegin[], _irqstackend[];

    do {
        lcd_clear_display();
        int line = 0;
        lcd_putsf(0, line++, "%-6s %6s %6s %4s", "stack", "used", "size", "pct");

        struct { const char* name; uint32_t* lo; uint32_t* hi; } s[] = {
            { "main", stackbegin,     stackend     },
            { "IRQ",  _irqstackbegin, _irqstackend },
        };

        for(unsigned i = 0; i < ARRAYLEN(s); ++i) {
            uint32_t* p = s[i].lo;
            while(p < s[i].hi && *p == 0xDEADBEEF)
                ++p;

            unsigned size = (unsigned)((char*)s[i].hi - (char*)s[i].lo);
            unsigned used = (unsigned)((char*)s[i].hi - (char*)p);
            lcd_putsf(0, line++, "%-6s %6u %6u %3u%%",
                      s[i].name, used, size, size ? (used * 100 / size) : 0);
        }

        line++;
        lcd_putsf(0, line++, "no guard page on either");
        lcd_putsf(0, line++, "IRQ overflows INTO main");

        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

static bool dbg_clocks(void)
{
    do {
        lcd_clear_display();
        int line = 0;
        for(int i = 0; i < X1600_CLK_COUNT; ++i) {
            uint32_t hz = clk_get(i);
            uint32_t khz = hz / 1000;
            uint32_t mhz = khz / 1000;
            lcd_putsf(0, line++, "%9s %4u,%03u,%03u Hz", clk_get_name(i),
                      mhz, (khz - mhz*1000), (hz - khz*1000));
        }

        /* The decoded CPU clock is NOT trustworthy on this SoC: the core comes
         * out of the boot ROM running far below what CPAPCR implies.  Show the
         * measured value next to it so the difference is visible.
         * clk_measure_cpu() times a loop against the OST; see clk-x1600.h. */
        uint32_t meas = clk_measure_cpu();
        uint32_t mkhz = meas / 1000;
        line++;
        lcd_putsf(0, line++, "%9s %4u,%03u,%03u Hz", "CPU(meas)",
                  mkhz / 1000, mkhz % 1000, meas - mkhz*1000);

        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

/* ------------------------------------------------------------------------
 * CPM
 * ------------------------------------------------------------------------ */

static bool dbg_cpm(void)
{
    do {
        lcd_clear_display();
        int line = 0;
        lcd_putsf(0, line++, "CPCCR  %08lx", (unsigned long)REG_CPM_CPCCR);
        lcd_putsf(0, line++, "CPCSR  %08lx", (unsigned long)REG_CPM_CPCSR);
        lcd_putsf(0, line++, "CPAPCR %08lx", (unsigned long)REG_CPM_CPAPCR);
        lcd_putsf(0, line++, "CPMPCR %08lx", (unsigned long)REG_CPM_CPMPCR);
        lcd_putsf(0, line++, "CPEPCR %08lx", (unsigned long)REG_CPM_CPEPCR);
        lcd_putsf(0, line++, "DDRCDR %08lx", (unsigned long)REG_CPM_DDRCDR);
        lcd_putsf(0, line++, "LPCDR  %08lx", (unsigned long)REG_CPM_LPCDR);
        lcd_putsf(0, line++, "MSC0CDR%08lx", (unsigned long)REG_CPM_MSC0CDR);
        lcd_putsf(0, line++, "MSC1CDR%08lx", (unsigned long)REG_CPM_MSC1CDR);
        lcd_putsf(0, line++, "SFCCDR %08lx", (unsigned long)REG_CPM_SFCCDR);
        lcd_putsf(0, line++, "SSICDR %08lx", (unsigned long)REG_CPM_SSICDR);
        lcd_putsf(0, line++, "PWMCDR %08lx", (unsigned long)REG_CPM_PWMCDR);
        lcd_putsf(0, line++, "I2S1CDR%08lx", (unsigned long)REG_CPM_I2S1CDR);
        line++;
        /* 1 = clock stopped.  Boot ROM leaves 0x47FBFFF4 / 0x37FFFFFF. */
        lcd_putsf(0, line++, "CLKGR  %08lx", (unsigned long)REG_CPM_CLKGR);
        lcd_putsf(0, line++, "CLKGR1 %08lx", (unsigned long)REG_CPM_CLKGR1);
        /* 1 = that block's RAM is powered down. */
        lcd_putsf(0, line++, "MEMPD0 %08lx", (unsigned long)REG_CPM_MEMPD0);
        lcd_putsf(0, line++, "MEMPD1 %08lx", (unsigned long)REG_CPM_MEMPD1);
        lcd_putsf(0, line++, "SRBC   %08lx", (unsigned long)REG_CPM_SRBC);
        lcd_putsf(0, line++, "OPCR   %08lx", (unsigned long)REG_CPM_OPCR);
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

/* ------------------------------------------------------------------------
 * GPIO
 * ------------------------------------------------------------------------ */

#define GPIO_NUM_PORTS 4    /* PA..PD; PM tables 21-2 .. 21-5 */

static const char gpio_portname[GPIO_NUM_PORTS] = { 'A', 'B', 'C', 'D' };

static void dbg_gpios_show_state(void)
{
    int line = 0;
    for(int i = 0; i < GPIO_NUM_PORTS; ++i)
        lcd_putsf(0, line++, "GPIO %c: %08lx", gpio_portname[i],
                  (unsigned long)REG_GPIO_PIN(i));

    line++;
    lcd_putsf(0, line++, "SFC pins are PC17-22 (mask %08lx)",
              (unsigned long)(0x3fu << 17));
    lcd_putsf(0, line++, "PC = %08lx", (unsigned long)REG_GPIO_PIN(GPIO_C));
}

static void dbg_gpios_show_config(int port)
{
    int line = 0;
    lcd_putsf(0, line++, "GPIO %c", gpio_portname[port]);
    /* NOTE the X1600 offsets: PxINT is at 0x10 (NOT 0x00 as on the X1000),
     * PxEDG at 0x70 is new, and the pull enable moved to 0x80. */
    lcd_putsf(0, line++, " int  %08lx", (unsigned long)REG_GPIO_INT(port));
    lcd_putsf(0, line++, " msk  %08lx", (unsigned long)REG_GPIO_MSK(port));
    lcd_putsf(0, line++, " pat1 %08lx", (unsigned long)REG_GPIO_PAT1(port));
    lcd_putsf(0, line++, " pat0 %08lx", (unsigned long)REG_GPIO_PAT0(port));
    lcd_putsf(0, line++, " flag %08lx", (unsigned long)REG_GPIO_FLAG(port));
    lcd_putsf(0, line++, " edg  %08lx", (unsigned long)REG_GPIO_EDG(port));
    lcd_putsf(0, line++, " pull %08lx", (unsigned long)REG_GPIO_PULL(port));
    lcd_putsf(0, line++, " pin  %08lx", (unsigned long)REG_GPIO_PIN(port));
    line++;
    lcd_putsf(0, line++, "LEFT/RIGHT change port");
}

static bool dbg_gpios(void)
{
    enum { STATE, NUM_STATIC_SCREENS };
    int screen = STATE;

    while(1) {
        lcd_clear_display();
        if(screen == STATE)
            dbg_gpios_show_state();
        else
            dbg_gpios_show_config(screen - NUM_STATIC_SCREENS);

        lcd_update();

        switch(get_action(CONTEXT_STD, screen == STATE ? 1 : HZ)) {
        case ACTION_STD_CANCEL:
            return false;
        case ACTION_STD_PREV:
        case ACTION_STD_PREVREPEAT:
            screen -= 1;
            if(screen < 0)
                screen = NUM_STATIC_SCREENS + GPIO_NUM_PORTS - 1;
            break;
        case ACTION_STD_NEXT:
        case ACTION_STD_NEXTREPEAT:
            screen += 1;
            if(screen >= NUM_STATIC_SCREENS + GPIO_NUM_PORTS)
                screen = 0;
            break;
        default:
            break;
        }
    }

    return false;
}

/* ------------------------------------------------------------------------
 * SADC
 * ------------------------------------------------------------------------ */

static bool dbg_adc(void)
{
    static const char* const adc_names[NUM_ADC_CHANNELS] = {
        "AUX0 buttons",
        "AUX1 hp det",
        "AUX2 remote",
        "AUX3 panel?",
    };

    do {
        lcd_clear_display();
        int line = 0;

        if(jz_readf(CPM_CLKGR, SADC)) {
            /* Reading SADC registers with the block gated would hang. */
            lcd_putsf(0, line++, "SADC is clock-gated (CLKGR bit 13)");
            lcd_putsf(0, line++, "adc_init() has not run");
        } else {
            for(int i = 0; i < NUM_ADC_CHANNELS; ++i) {
                unsigned short raw = adc_read(i);
                lcd_putsf(0, line++, "%-13s %4u  %4d mV",
                          adc_names[i], raw, adc_read_mv(i));
            }

            line++;
            lcd_putsf(0, line++, "key idle %d mV", ADC_KEY_IDLE_MV);
            lcd_putsf(0, line++, "key play %d  vol- %d  vol+ %d",
                      ADC_KEY_PLAY_MV, ADC_KEY_VOLDOWN_MV, ADC_KEY_VOLUP_MV);
            lcd_putsf(0, line++, "hp in-range %d..%d mV",
                      ADC_HP_MIN_MV, ADC_HP_MAX_MV);
        }

        lcd_update();
    } while(get_action(CONTEXT_STD, HZ/4) != ACTION_STD_CANCEL);

    return false;
}



/* ------------------------------------------------------------------------
 * Audio
 * ------------------------------------------------------------------------ */

static bool dbg_audio(void)
{
    do {
        lcd_clear_display();
        int line = 0;
        /* pcm-x1600.c publishes this the same way ingenic_x1000 does -- a
         * plain volatile counter bumped in the underrun path, declared extern
         * at the top of this file rather than via a header, matching
         * debug-x1000.c:119.  There is no RX counterpart here because this
         * port has no recording path. */
        lcd_putsf(0, line++, "TX underruns            %u", aic_tx_underruns);
        lcd_putsf(0, line++, "AUDIO gate (CLKGR.11)   %s",
                  jz_readf(CPM_CLKGR, AUDIO) ? "GATED" : "open");
        lcd_putsf(0, line++, "I2S0 TCLK  (CLKGR1.9)   %s",
                  jz_readf(CPM_CLKGR1, I2S0_TCLK) ? "GATED" : "open");
        lcd_putsf(0, line++, "I2S0 RCLK  (CLKGR1.8)   %s",
                  jz_readf(CPM_CLKGR1, I2S0_RCLK) ? "GATED" : "open");
        lcd_putsf(0, line++, "I2S1CDR   %08lx",
                  (unsigned long)REG_CPM_I2S1CDR);
        lcd_putsf(0, line++, "MCLK      %lu Hz",
                  (unsigned long)clk_get(X1600_CLK_I2S_MCLK));
        lcd_putsf(0, line++, "BCLK      %lu Hz",
                  (unsigned long)clk_get(X1600_CLK_I2S_BCLK));

        /* Silence has two halves, and these two lines tell them apart.
         *
         *   nak == w      the control bus never reached the DAC, so nothing was
         *                 ever configured. The part reads back 0x00 for every
         *                 register, so this ACK count is the only evidence
         *                 available that it is there at all. e-2 = no ACK on
         *                 the slave address.
         *   TFL stuck 0   the PDMA never delivers samples to the AIC; suspect
         *                 DMA_CHN_RT TYPE (see pcm-x1600.c).
         *
         * AIC_SR is read only when the block is ungated -- reading a gated
         * peripheral hangs the bus, and a debug screen that wedges the device
         * is worse than no debug screen. */
        line++;
#ifdef HAVE_CS43131
        {
            unsigned w, n; int e;
            cs43131_bus_status(&w, &n, &e);
            lcd_putsf(0, line++, "DAC i2c   w%u nak%u e%d", w, n, e);
        }
#endif
        if(jz_readf(CPM_CLKGR, AUDIO)) {
            lcd_putsf(0, line++, "AIC_SR    (gated)");
        } else {
            unsigned long sr = REG_AIC_SR;
            lcd_putsf(0, line++, "AIC_SR    %08lx TFL %lu", sr,
                      (sr & BM_AIC_SR_TFL) >> BP_AIC_SR_TFL);
        }
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

/* ------------------------------------------------------------------------
 * Menu
 * ------------------------------------------------------------------------ */

#ifdef HAVE_AXP2101
extern bool axp2101_debug_menu(void);
#endif
#ifdef HAVE_CW2015
extern bool cw2015_debug_menu(void);
#endif

/* SD write round-trip, which exercises the shared MSC driver, whose
 * write path had never been exercised on hardware.
 *
 * LBA 1000 sits in the gap between the MBR and the first partition, so save,
 * write, read back, compare, restore costs nothing -- and it refuses unless the
 * sector is demonstrably empty, because some tools put boot code there and this
 * is someone else's card. Not sector 0: that is the MBR.
 *
 * Every path reports "did not run" distinctly from a result. An earlier version
 * of this screen rendered a tri-state with a boolean test, so the sentinel -1
 * displayed as success. */
static bool dbg_sd_write(void)
{
    static unsigned char wsave[512] __attribute__((aligned(64)));
    static unsigned char wtest[512] __attribute__((aligned(64)));
    static unsigned char wback[512] __attribute__((aligned(64)));
    int state = -1;             /* -1 not run, -2 skipped, 0 mismatch, 1 ok */
    int firstbad = -1;

    if(sd_read_sectors(IF_MD(0,) 1000, 1, wsave) == 0) {
        int empty = 1;
        for(int i = 0; i < 512; ++i)
            if(wsave[i] != 0x00) { empty = 0; break; }

        if(!empty) {
            state = -2;
        } else {
            for(int i = 0; i < 512; ++i)
                wtest[i] = (unsigned char)(i * 13 + 7);   /* offset-sensitive */

            if(sd_write_sectors(IF_MD(0,) 1000, 1, wtest) == 0 &&
               sd_read_sectors(IF_MD(0,) 1000, 1, wback) == 0) {
                state = 1;
                for(int i = 0; i < 512; ++i)
                    if(wback[i] != wtest[i]) { state = 0; firstbad = i; break; }
            } else {
                state = 0;
            }

            /* Put the sector back whatever happened */
            sd_write_sectors(IF_MD(0,) 1000, 1, wsave);
        }
    }

    logf("x1600 SD write round-trip: %s",
         state == -2 ? "SKIPPED (LBA 1000 not empty)" :
         state <  0  ? "could not run" :
         state       ? "OK" : "MISMATCH");

    do {
        lcd_clear_display();
        lcd_putsf(0, 0, "SD WRITE ROUND-TRIP");
        lcd_putsf(0, 2, "LBA 1000, 512 B");
        lcd_putsf(0, 4, "result  %s",
                  state == -2 ? "SKIPPED, not empty" :
                  state <  0  ? "?? DID NOT RUN" :
                  state       ? "OK" : "MISMATCH");
        if(state == 0 && firstbad >= 0)
            lcd_putsf(0, 5, "first bad byte  %d", firstbad);
        /* Only the path that wrote the sector restored it; saying so
         * unconditionally reports work that did not happen. */
        lcd_putsf(0, 7, state >= 0 ? "sector restored" : "sector untouched");
        lcd_update();
    } while(get_action(CONTEXT_STD, HZ) != ACTION_STD_CANCEL);

    return false;
}

const struct ingenic_debug_menuitem ingenic_debug_menu[] = {
    {"Cache mode", &dbg_cache_mode},
    {"Stacks", &dbg_stacks},
    {"SD write test", &dbg_sd_write},
    {"Clocks", &dbg_clocks},
    {"CPM registers", &dbg_cpm},
    {"GPIOs", &dbg_gpios},
#ifdef INGENIC_CPUIDLE_STATS
    {"CPU idle", &dbg_cpuidle},
#endif
    {"SADC", &dbg_adc},
    {"Audio", &dbg_audio},
#ifdef HAVE_AXP2101
    {"Power stats (AXP2101)", &axp2101_debug_menu},
#endif
#ifdef HAVE_CW2015
    {"CW2015 debug", &cw2015_debug_menu},
#endif
};

const int ingenic_debug_menu_count = ARRAYLEN(ingenic_debug_menu);




bool dbg_ports(void)
{
    /* Live button state, showing both the driver's own view
     * (button_read_device) and the value after the generic layer
     * (button_status): a difference between them localises a fault to the
     * driver or to the keymap/queue immediately.  The keys are an ADC ladder,
     * so a misread is a threshold, not a stuck GPIO. */
    lcd_setfont(FONT_SYSFIXED);

    while(1) {
        int data = 0;
        int raw = button_read_device(&data);
        int st  = button_status();
        static int last_raw = -1;
        if(raw != last_raw) {       /* only on change, or the log fills with noise */
            logf("x1600 button: raw=%08X status=%08X", raw, st);
            last_raw = raw;
        }

        lcd_clear_display();
        lcd_putsf(0, 0, "BUTTON DEBUG");
        lcd_putsf(0, 2, "raw    %08X", raw);
        lcd_putsf(0, 3, "status %08X", st);
        lcd_putsf(0, 5, "POWER  %s", (raw & BUTTON_POWER) ? "DOWN" : "up");
        lcd_putsf(0, 6, "PLAY   %s", (raw & BUTTON_PLAY)  ? "DOWN" : "up");
        lcd_putsf(0, 7, "NEXT   %s", (raw & BUTTON_NEXT)  ? "DOWN" : "up");
        lcd_putsf(0, 8, "VOL+   %s", (raw & BUTTON_VOL_UP)   ? "DOWN" : "up");
        lcd_putsf(0, 9, "VOL-   %s", (raw & BUTTON_VOL_DOWN) ? "DOWN" : "up");
        lcd_putsf(0, 11,"hold PLAY to exit");
        lcd_update();

        /* PLAY, not POWER: POWER is the button under investigation and must
         * stay observable rather than being consumed to leave the screen. */
        int b = button_get_w_tmo(HZ/10);
        if(b == (BUTTON_PLAY | BUTTON_REPEAT))
            break;
    }

    lcd_setfont(FONT_UI);
    return false;
}
#endif /* !BOOTLOADER */
