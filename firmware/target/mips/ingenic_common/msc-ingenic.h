/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
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

/* MSC (SD/MMC controller) interface, shared by the X1000 and the X1600.
 *
 * The implementation is shared too, in ingenic_common/msc-ingenic.c. What is
 * genuinely per-SoC or per-board is reached through the three hooks at the
 * bottom of this file.
 *
 * ⚠ Do not give a target its own copy of this header. sd-ingenic.c is shared
 * and consumes 39 symbols defined here -- msc_req, msc_drv, MSC_RESP_R1,
 * MSC_RF_AUTO_CMD12 and the rest. Two definitions would not fail to compile if
 * they drifted: a moved bit in MSC_RF_WRITE or a reordered msc_req field shows
 * up as a corrupted card.
 *
 * A target whose MSC needs board-level pins defines SOC_MSC_HAS_BOARD_PINS
 * before including this header, which adds the card-power and pin-group fields
 * to msc_config. It is opt-in so the default stays honest: the X1600 defines
 * it, the X1000 has no counterpart.
 */

#ifndef __MSC_INGENIC_H__
#define __MSC_INGENIC_H__

#include "kernel.h"
#include "sdmmc.h"
#include <stdbool.h>
#include <stdint.h>

/* Number of MSC controllers. Two on both parts (X1600 PM Table 26-4). */
#define MSC_COUNT 2

/* Media types */
#define MSC_TYPE_SD  0
#define MSC_TYPE_MMC 1
#define MSC_TYPE_ATA 2
#define MSC_TYPE_ANY 3

/* Clock modes */
#define MSC_CLK_MANUAL    0
#define MSC_CLK_AUTOMATIC 1

/* Clock status bits */
#define MSC_CLKST_ENABLE (1 << 0)
#define MSC_CLKST_AUTO   (1 << 1)

/* Driver flags */
#define MSC_DF_ERRSTATE (1 << 0)
#define MSC_DF_READY    (1 << 1)
#define MSC_DF_HCS_CARD (1 << 2)
#define MSC_DF_V2_CARD  (1 << 3)
#define MSC_DF_HAS_SBC  (1 << 4)

/* Request status codes */
#define MSC_REQ_SUCCESS     0
#define MSC_REQ_CRC_ERR     1
#define MSC_REQ_CARD_ERR    2
#define MSC_REQ_TIMEOUT     3
#define MSC_REQ_EXTRACTED   4
#define MSC_REQ_LOCKUP      5
#define MSC_REQ_ERROR       6
#define MSC_REQ_INCOMPLETE  (-1)

/* Response types */
#define MSC_RESP_NONE   0
#define MSC_RESP_BUSY   (1 << 7)
#define MSC_RESP_R1     1
#define MSC_RESP_R1B    (MSC_RESP_R1|MSC_RESP_BUSY)
#define MSC_RESP_R2     2
#define MSC_RESP_R3     3
#define MSC_RESP_R6     6
#define MSC_RESP_R7     7

/* Request flags */
#define MSC_RF_INIT         (1 << 0)
#define MSC_RF_ERR_CMD12    (1 << 1)
#define MSC_RF_AUTO_CMD12   (1 << 2)
#define MSC_RF_PROG         (1 << 3)
#define MSC_RF_DATA         (1 << 4)
#define MSC_RF_WRITE        (1 << 5)
#define MSC_RF_ABORT        (1 << 6)

/* Clock speeds */
#define MSC_SPEED_INIT  400000
#define MSC_SPEED_FAST  25000000
#define MSC_SPEED_HIGH  50000000

typedef struct msc_config {
    int msc_nr;
    int msc_type;
    int bus_width;
    const char* label;

    /* Card detect pin, or GPIO_NONE for a non-removable card. */
    int cd_gpio;
    int cd_active_level;

#ifdef SOC_MSC_HAS_BOARD_PINS
    /* Card power switch, or GPIO_NONE if the slot is permanently powered.
     * R1: PC25, active high (the vendor device tree's msc1 node carries
     * msc1_pwr_enable_level = 1).  Note PC25 is one of the three pads whose
     * pull resistor points DOWN rather than up (X1600 PM 21.4.2.19), so the
     * pin must actually be driven, not merely released. */
    int pwr_gpio;
    int pwr_active_level;

    /* Pin group carrying clk/cmd/d0..d3, applied by the driver so that the
     * MSC mux is guaranteed to be right even if gpio_init()'s table is later
     * reorganized.  R1/MSC1: port D, pins PD00-PD05, device function 0
     * (X1600 PM Table 21-5, line 19662: PD00 msc1_clk_o, PD01 msc1_cmd,
     * PD02..PD05 msc1_d0..d3, all Fun_0). */
    int pin_port;
    uint32_t pin_mask;
    int pin_func;
#endif
} msc_config;

typedef struct msc_req {
    /* Filled by caller */
    int command;
    unsigned argument;
    int resptype;
    int flags;
    void* data;
    unsigned nr_blocks;
    unsigned block_len;

    /* Filled by driver */
    volatile unsigned response[4];
    volatile int status;
} msc_req;

/* SDMA descriptor, X1600 PM 26.8.2.1 "Structure of SDMA Descriptor"
 * (line 27087).  Four words: NDA / DA / LEN / CMD.  The PM requires NDA to be
 * "4-word aligned", hence aligned(16); the X1000's layout is identical. */
struct sd_dma_desc {
    unsigned nda;
    unsigned mem;
    unsigned len;
    unsigned cmd;
} __attribute__((aligned(16)));

typedef struct msc_drv {
    int msc_nr;
    int drive_nr;
    const msc_config* config;

    int driver_flags;
    int clk_status;
    unsigned cmdat_def;
    msc_req* req;
    unsigned iflag_done;

    volatile int req_running;
    volatile int card_present; /* Debounced status */
    volatile int card_present_last; /* Status when we last polled it */

    struct mutex lock;
    struct semaphore cmd_done;
    struct timeout cmd_tmo;
    struct timeout cd_tmo;
    struct sd_dma_desc dma_desc;

    tCardInfo cardinfo;
} msc_drv;

/* Driver initialization, etc */
extern void msc_init(void);
extern msc_drv* msc_get(int type, int index);
extern msc_drv* msc_get_by_drive(int drive_nr);

extern void msc_lock(msc_drv* d);
extern void msc_unlock(msc_drv* d);
extern void msc_full_reset(msc_drv* d);
extern bool msc_card_detect(msc_drv* d);

extern void msc_led_trigger(void);

/* Controller API */
extern void msc_ctl_reset(msc_drv* d);
extern void msc_set_clock_mode(msc_drv* d, int mode);
extern void msc_enable_clock(msc_drv* d, bool enable);
extern void msc_set_speed(msc_drv* d, int rate);
extern void msc_set_width(msc_drv* d, int width);

/* Request API */
extern void msc_async_start(msc_drv* d, msc_req* r);
extern void msc_async_abort(msc_drv* d, int status);
extern int  msc_async_wait(msc_drv* d, int timeout);
extern int  msc_request(msc_drv* d, msc_req* r);

/* Command helpers; note these are written with SD in mind
 * and should be reviewed before using them for MMC / CE-ATA
 */
extern int msc_cmd_exec(msc_drv* d, msc_req* r);
extern int msc_app_cmd_exec(msc_drv* d, msc_req* r);
extern int msc_cmd_go_idle_state(msc_drv* d);
extern int msc_cmd_send_if_cond(msc_drv* d);
extern int msc_cmd_app_op_cond(msc_drv* d);
extern int msc_cmd_all_send_cid(msc_drv* d);
extern int msc_cmd_send_rca(msc_drv* d);
extern int msc_cmd_send_csd(msc_drv* d);
extern int msc_cmd_select_card(msc_drv* d);
extern int msc_cmd_set_bus_width(msc_drv* d, int width);
extern int msc_cmd_set_clr_card_detect(msc_drv* d, int arg);
extern int msc_cmd_switch_freq(msc_drv* d);
extern int msc_cmd_send_status(msc_drv* d);
extern int msc_cmd_set_block_len(msc_drv* d, unsigned len);

/* ---------------------------------------------------------------------------
 * Supplied by the target (msc-x1000.c / msc-x1600.c)
 *
 * These three are the whole of what differs between the two SoCs. Keep it that
 * way: anything added here has to be maintained twice, which is what folding
 * the drivers together was meant to stop.
 */

/* Controller table, terminated by an entry with msc_nr == -1. */
extern const msc_config msc_configs[];

/* Select the MSC clock parent, once, during msc_init(). Called before the
 * controller is ungated, so it may only touch CPM. */
extern void msc_soc_init_clock(int msc);

/* Program the CPM divider so the controller is fed at no more than `rate` Hz,
 * and return the frequency actually presented -- which the driver then divides
 * further with MSC_CLKRT. Called with the card clock stopped. */
extern unsigned msc_soc_set_clock(int msc, unsigned rate);

#endif /* __MSC_INGENIC_H__ */
