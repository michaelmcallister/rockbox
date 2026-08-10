/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2026 Michael McAllister
 * Structure follows bootloader/x1000/main.c,
 * Copyright (C) 2021 Aidan MacDonald
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/

/* Boot beacon: a breadcrumb in SRAM that survives the stage2 jump, so a boot
 * that dies says where. Raised from boot.c, gui.c, recovery.c and main.c. */

#include "x1600bootloader.h"
#include "system.h"
#include "core_alloc.h"
#include "kernel/kernel-internal.h"
#include "power.h"
#include "button.h"
#include "adc.h"
#include "storage.h"
#include "disk.h"
#include "file_internal.h"
#include "usb.h"
#include "rb-loader.h"
#include "loader_strerror.h"
#include "boot-x1600.h"
#include "installer-ingenic.h"
#include "lcd.h"
#include "backlight.h"
#include "font.h"
#include "i2c-ingenic.h"
#include "i2c-target.h"
#include "dpu-x1600.h"
#include "x1600/intc.h"   /* REG_INTC_* for the INTC diagnostic */
#include "x1600/msc.h"    /* REG_MSC_*  for the storage diagnostic */
#include "clk-x1600.h"    /* clk_get() -- is the MSC clock even alive? */
#include "x1600/cpm.h"

/* 0 = not run, 0xFFFFFFFF = the tick never moved, else iterations taken */
volatile uint32_t bl_tick_probe;

/* Volumes mounted, and whether the SD driver sees a card at all */
volatile int32_t bl_mounts   = -1;
volatile int32_t bl_sd_state = -1;

/* AXP2101 rail-enable (0x90) after power_init(), -1 until then. bldo1/bldo2 are
 * the panel VCC/VCCIO and boot_on = 0, so worth reading back rather than assuming */
volatile int32_t bl_pmu_en = -1;

/* SRAM at 0x8000f200 survives a stage2 jump and the USB port reset that gets
 * the BootROM link back, so the host can read how far the boot got. A
 * SUCCESSFUL boot overwrites it -- Rockbox loads across that address -- so it
 * only means anything on a boot that failed. */
#define BL_BEACON_ADDR   0xa000f200      /* KSEG1 alias of SRAM 0x8000f200 */

/* Three copies, majority-voted per bit by the host, 4 KiB apart so they land
 * in different DRAM rows */
#define BL_BEACON_COPIES 3
#define BL_BEACON_STRIDE 0x1000
#define BL_BEACON_FORMAT 9               /* +both ends of the fb copy */
#define BL_BEACON_WORDS  20
/* KSEG1 maps physical P to 0xA0000000+P: the uncached view of 0x8000f200 is
 * 0xa000f200, NOT 0xb000f200 -- that is peripheral space */

/* Names for the on-screen trace. Indexed by stage, so the order must match the
 * enum above exactly; index 0 is unused because BL_ST_ENTRY starts at 1. */

/* PROGRAM_START2 is one-way: the BootROM stops the instant we jump, so no
 * stage is observable in real time and the beacon is the only record. */

/* One beacon record, assembled in RAM and then written to all three copies. */
static uint32_t bl_bcn[BL_BEACON_WORDS];

/* A mark in CPSPR: a breadcrumb no reset clears, so a wedged run still says
 * where. [31:30] are the boot flags and must be preserved; [15:0] are the mark. */
static inline void bringup_mark(uint16_t code)
{
    uint32_t v = (REG_CPM_CPSPR & ~0x0000ffffu) | code;
    REG_CPM_CPSPPR = 0x5a5a;    /* unlock, PM 11.1.2.22 */
    REG_CPM_CPSPR  = v;
    REG_CPM_CPSPPR = 0xa5a5;    /* re-lock */
}

/* Bootloader stages occupy 0x1000|stage in the CPSPR mark field, keeping them
 * clear of the probe payloads' own marks at 0x0000-0x00ff. */
#define BL_MARK(stage)   (0x1000u | ((stage) & 0xff))

void bl_beacon(uint32_t stage)
{
    uint32_t* b = bl_bcn;

    /* CPSPR survives what DRAM does not -- reading the DRAM copies needs a
     * power-off, and this DRAM decays to zero. Read it first: usbstage1 clears
     * it after sampling. PM 11.1.2.23 */
    bringup_mark(BL_MARK(stage));


    b[0] = 0xB0075EEDu;     /* "BOOTSEED" */
    b[1] = stage;
    b[2] += 1;              /* how many stages we passed, survives a later hang */

    /* The tick: the MSC wait is only rescued by a tick-driven timeout, so a
     * stopped tick and a stalled transfer look identical from outside */
    b[3] = (uint32_t)current_tick;

    /* Reserved; written so the host never reads stale memory here */
    b[4] = 0;
    b[5] = bl_tick_probe;

    /* Format stamp, written last and checked first, so leftover memory cannot
     * read back as a plausible beacon. Bump when a word changes meaning */
    b[6] = (uint32_t)bl_mounts;
    b[7] = (uint32_t)bl_sd_state;

    /* DPU liveness, read from the block rather than assumed. The vendor kernel
     * with the panel lit had DC_ST = 0x00000006 (SRD_START|SRD_END). */
    b[8]  = dpu_debug_snapshot(0);   /* DC_ST          */
    b[9]  = dpu_debug_snapshot(1);   /* DISP_COM       */
    b[10] = dpu_debug_snapshot(2);   /* SRD_CHAIN_ADDR */
    b[11] = (uint32_t)bl_pmu_en;     /* AXP 0x90: bit4 panel VCC, bit5 VCCIO */
    b[12] = dpu_debug_snapshot(8);   /* fbcopy runs     */
    b[13] = dpu_debug_snapshot(9);   /* fbcopy timeouts */
    b[14] = dpu_debug_snapshot(10);  /* dpu_stop non-acks */
    b[15] = clk_timeouts;            /* PLL/mux waits that never settled */
    /* Both ends of the framebuffer copy: without the source, a black panel
     * cannot tell a copy that never ran from one that copied zeros */
    b[17] = dpu_debug_snapshot(13);  /* lcd_fbbase, the copy SOURCE       */
    b[18] = dpu_debug_snapshot(14);  /* first pixel AT the source         */
    b[19] = dpu_debug_snapshot(15);  /* first pixel of the scanout buffer */
    b[16] = BL_BEACON_FORMAT;

    /* Publish: three copies, so per-bit majority voting on the host can
     * reconstruct the record even after a power-cycle has decayed each one. */
    for(int c = 0; c < BL_BEACON_COPIES; ++c) {
        volatile uint32_t* dst =
            (volatile uint32_t*)(BL_BEACON_ADDR + c * BL_BEACON_STRIDE);
        for(int i = 0; i < BL_BEACON_WORDS; ++i)
            dst[i] = b[i];
    }

}
