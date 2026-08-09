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

#include "system.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"
#include "led.h"
#include "sdmmc_host.h"
#include "ingenic-soc.h"
#include "gpio-ingenic.h"
#include <string.h>
#include <stddef.h>

/* #define LOGF_ENABLE */
#include "logf.h"

#define DEBOUNCE_TIME (HZ/10)

const msc_config msc_configs[] = {
#if defined(HIBY_R1_NATIVE)
/* Clock source. MPLL at 1400 MHz gives 700 MHz after its fixed /2, which
 * divides exactly to both 25 and 50 MHz. SCLK_A would work arithmetically but
 * is also subject to CPCCR.GATE_SCLKA; EPLL is the audio/display parent.
 * TODO(x1600): unverified -- the vendor's own MSC1 parent was never captured.
 * ⚠ Reading CPM_MSC1CDR back does NOT answer this:
 * it read 0x4000000d consistently, which is only what msc_soc_set_clock() had
 * just written. The register reports our choice, not the vendor's. Answering it
 * needs a capture from the stock firmware, or a read before our driver runs.
 * msc_soc_set_clock() panics rather than dividing by zero if it is stopped. */
#define MSC_CLOCK_SOURCE  X1600_CLK_MPLL
#define MSC_CLOCK_MPCS    BV_CPM_MSC1CDR_MPCS__MPLL
    {
        /* !! MSC *1* !!  From /sys/kernel/debug/gpio labels
         * (msc1_clk, msc1_cmd, msc1_d0..d3) and PM Table 21-5 (line 19662). */
        .msc_nr = 1,
        .storage_type = STORAGE_SD,
        .bus_voltages = SDMMC_BUS_VOLTAGE_3V2_3V3 |
                        SDMMC_BUS_VOLTAGE_3V3_3V4,
        .bus_widths = SDMMC_BUS_WIDTH_1BIT |
                      SDMMC_BUS_WIDTH_4BIT,
        .bus_clocks = SDMMC_BUS_CLOCK_400KHZ |
                      SDMMC_BUS_CLOCK_25MHZ |
                      SDMMC_BUS_CLOCK_50MHZ,
        .cd_gpio = GPIO_MSC1_CD,        /* PB22 */
        .cd_active_level = 0,           /* vendor DT: cd-inverted, level 0 */
        .pwr_gpio = GPIO_MSC1_POWER,    /* PC25 */
        .pwr_active_level = 1,          /* vendor DT: msc1_pwr_enable_level=1 */
        .pin_port = GPIO_D,
        .pin_mask = 0x3f << 0,          /* PD00..PD05 */
        .pin_func = GPIOF_DEVICE(0),    /* PM Table 21-5 Fun_0 = msc1_* */
    },
    /* NOTE: MSC0 is the SDIO wifi module (PB12-PB17) and is deliberately NOT
     * listed.  Rockbox has no use for it, its CLKGR gate is left closed by
     * system_init(), and its INTC source is never unmasked. */
#else
# error "Please add X1600 MSC config"
#endif
    {.msc_nr = -1},
};

/* Ratio = (reg + 1) * 2, so the field holds ratio/2 - 1 and the representable
 * ratios are the even numbers 2..512 (PM 11.1.2.9). The write-with-CE,
 * poll-BUSY, clear-CE sequence is the X1000's. */
static void msc_set_cdr(int msc, uint32_t mpcs, uint32_t ratio)
{
    if(ratio < 2)
        ratio = 2;
    else if(ratio > 512)
        ratio = 512;

    uint32_t regdiv = (ratio / 2) - 1;

    if(msc == 0) {
        jz_writef(CPM_MSC0CDR, CE_MSC(1), MPCS(mpcs), MSCCDR(regdiv));
        (void)x1600_spin_while(jz_readf(CPM_MSC0CDR, MSC_BUSY));
        jz_writef(CPM_MSC0CDR, CE_MSC(0));
    } else {
        jz_writef(CPM_MSC1CDR, CE_MSC(1), MPCS(mpcs), MSCCDR(regdiv));
        (void)x1600_spin_while(jz_readf(CPM_MSC1CDR, MSC_BUSY));
        jz_writef(CPM_MSC1CDR, CE_MSC(0));
    }
}

void msc_soc_init_clock(int msc)
{
    /* msc_set_speed() reprograms this a few instructions later, so the ratio
     * only has to survive that gap. The X1000 writes the smallest ratio here,
     * which would briefly feed the MSC 700 MHz; the largest costs nothing and
     * keeps the transient at 2.7 MHz. */
    msc_set_cdr(msc, MSC_CLOCK_MPCS, 512);
}

unsigned msc_soc_set_clock(int msc, unsigned rate)
{
    /* The /2 mirrors the register's fixed extra halving (PM 11.1.2.9); the
     * X1000 tags both MSC clocks INCLK_SHIFT(1) for the same effect. */
    uint32_t src_freq = clk_get(MSC_CLOCK_SOURCE) / 2;

    /* clk_init() leaves MPLL and EPLL alone, so entering Rockbox without a
     * bootloader having started the chosen PLL gives clk_get() == 0. Dividing
     * by that would turn a missing clock into an unexplained lockup. */
    if(UNLIKELY(src_freq == 0)) {
        panicf("msc%d: clock source %s is stopped", msc,
               clk_get_name(MSC_CLOCK_SOURCE));
    }

    uint32_t div = clk_calc_div(src_freq, rate);
    msc_set_cdr(msc, MSC_CLOCK_MPCS, 2 * div);
    return src_freq / div;
}

/* MSC_NOB is 16 bits wide */
#define MSC_MAX_BLOCKS 65535

/* The controller should always raise an interrupt, but if it does not then
 * we must not leave the storage thread blocked forever.
 *
 * TODO: calculate a suitable lower value for the lockup timeout.
 *
 * The SD spec defines timings based on the number of blocks transferred,
 * see sec. 4.6.2 "Read, write, and erase timeout conditions". This should
 * reduce the long delays which happen if errors occur.
 *
 * Also need to check if registers MSC_RDTO / MSC_RESTO are correctly set.
 */
#define COMMAND_TIMEOUT (10*HZ)

struct sd_dma_desc {
    unsigned nda;
    unsigned mem;
    unsigned len;
    unsigned cmd;
} __attribute__((aligned(16)));

typedef struct msc_drv {
    int msc_nr;
    const msc_config* config;
    struct sdmmc_host host;

    /* Default CMDAT; carries the bus width */
    unsigned cmdat_def;

    /* Bus clock requested by sdmmc_host, and whether it is programmed */
    uint32_t bus_clock;
    bool powered;

    /* Set after power on so the next command carries the initialization
     * clock sequence the card needs before CMD0 */
    bool send_init;

    /* Command state; shared between the caller and the interrupt handler */
    struct sdmmc_host_response* resp;
    unsigned resp_length;
    void* data_buf;
    unsigned data_len;
    bool data_present;
    bool data_write;
    unsigned iflag_done;
    volatile int status;
    volatile int req_running;

    /* Debounced card detect state */
    volatile int card_present;
    volatile int card_present_last;

    struct semaphore cmd_done;
    struct timeout cmd_tmo;
    struct timeout cd_tmo;
    struct sd_dma_desc dma_desc;
} msc_drv;

static msc_drv msc_drivers[MSC_COUNT];

static void msc0_cd_interrupt(void);
static void msc1_cd_interrupt(void);

static const msc_config* msc_lookup_config(int msc)
{
    for(int i = 0; i < MSC_COUNT; ++i)
        if(msc_configs[i].msc_nr == msc)
            return &msc_configs[i];
    return NULL;
}

static void msc_gate_clock(int msc, bool gate)
{
    int bit;
    if(msc == 0)
        bit = BM_CPM_CLKGR_MSC0;
    else
        bit = BM_CPM_CLKGR_MSC1;

    if(gate)
        REG_CPM_CLKGR |= bit;
    else
        REG_CPM_CLKGR &= ~bit;
}

static bool msc_card_detect(msc_drv* d)
{
    if(d->config->cd_gpio == GPIO_NONE)
        return true;

    return gpio_get_level(d->config->cd_gpio) == d->config->cd_active_level;
}

static void msc_led_trigger(void)
{
    bool state = false;
    for(int i = 0; i < MSC_COUNT; ++i)
        if(msc_drivers[i].req_running)
            state = true;

    led(state);
}

/* ---------------------------------------------------------------------------
 * Controller setup
 */

static void msc_ctl_reset(msc_drv* d)
{
    /* Ingenic code suggests a reset changes clkrt */
    int clkrt = REG_MSC_CLKRT(d->msc_nr);

    /* Send reset -- bit is NOT self clearing */
    jz_overwritef(MSC_CTRL(d->msc_nr), RESET(1));
    udelay(100);
    jz_writef(MSC_CTRL(d->msc_nr), RESET(0));

    /* Verify reset in the status register */
    long deadline = current_tick + HZ;
    while(jz_readf(MSC_STAT(d->msc_nr), IS_RESETTING) &&
          current_tick < deadline) {
        sleep(1);
    }

    /* Let the controller gate the bus clock while the bus is idle */
    jz_writef(MSC_CTRL(d->msc_nr), CLOCK_V(STOP));
    jz_writef(MSC_LPM(d->msc_nr), ENABLE(1));

    /* Clear and mask interrupts */
    REG_MSC_IMASK(d->msc_nr) = 0xffffffff;
    REG_MSC_IFLAG(d->msc_nr) = 0xffffffff;

    /* Restore clkrt */
    REG_MSC_CLKRT(d->msc_nr) = clkrt;
}

static unsigned msc_clock_rate(uint32_t clock)
{
    switch(clock) {
    case SDMMC_BUS_CLOCK_400KHZ: return MSC_SPEED_INIT;
    case SDMMC_BUS_CLOCK_25MHZ:  return MSC_SPEED_FAST;
    case SDMMC_BUS_CLOCK_50MHZ:  return MSC_SPEED_HIGH;
    default: panicf("%s: bad clock", __func__);
    }
}

static void msc_apply_bus_clock(msc_drv* d)
{
    unsigned rate = msc_clock_rate(d->bus_clock);

    /* Wait for clock to go idle */
    SOC_SPIN_WHILE_DO(jz_readf(MSC_STAT(d->msc_nr), CLOCK_EN), sleep(1));

    /* freq1 is output by the SOC clock divider; freq2 by MSC_CLKRT */
    unsigned freq1 = rate < MSC_SPEED_FAST ? MSC_SPEED_FAST : rate;
    unsigned in_freq = msc_soc_set_clock(d->msc_nr, freq1);

    unsigned clkrt = clk_calc_shift(in_freq, rate);
    REG_MSC_CLKRT(d->msc_nr) = clkrt;

    /* Handle frequency dependent timing settings
     * TODO - these settings might be SD specific...
     */
    if((in_freq >> clkrt) > MSC_SPEED_FAST) {
        jz_writef(MSC_LPM(d->msc_nr),
                  DRV_SEL_V(RISE_EDGE_DELAY_QTR_PHASE),
                  SMP_SEL_V(RISE_EDGE_DELAYED));
        jz_writef(MSC_CTRL2(d->msc_nr), SPEED_V(HIGHSPEED));
    } else {
        jz_writef(MSC_LPM(d->msc_nr),
                  DRV_SEL_V(FALL_EDGE),
                  SMP_SEL_V(RISE_EDGE));
        jz_writef(MSC_CTRL2(d->msc_nr), SPEED_V(DEFAULT));
    }
}

/* ---------------------------------------------------------------------------
 * Request handling
 */

/* Note -- this must only be called with IRQs disabled */
static void msc_finish_request(msc_drv* d, int status)
{
    REG_MSC_IMASK(d->msc_nr) = 0xffffffff;
    REG_MSC_IFLAG(d->msc_nr) = 0xffffffff;
    if(d->data_present)
        jz_writef(MSC_DMAC(d->msc_nr), ENABLE(0));

    d->status = status;
    d->req_running = 0;
    d->iflag_done = 0;

    msc_led_trigger();
    timeout_cancel(&d->cmd_tmo);
    semaphore_release(&d->cmd_done);
}

static int msc_req_timeout(struct timeout* tmo)
{
    msc_drv* d = (msc_drv*)tmo->data;
    int irq = disable_irq_save();

    if(d->req_running) {
        logf("msc%d: command lockup", d->msc_nr);
        msc_finish_request(d, SDMMC_STATUS_ERROR);
    }

    restore_irq(irq);
    return 0;
}

static void msc_read_response(msc_drv* d)
{
    uint32_t data[4] = {0};
    unsigned res = REG_MSC_RES(d->msc_nr);
    unsigned dat;

    switch(d->resp_length) {
    case SDMMC_RESP_SHORT:
        dat = res << 24;
        res = REG_MSC_RES(d->msc_nr);
        dat |= res << 8;
        res = REG_MSC_RES(d->msc_nr);
        dat |= res & 0xff;
        data[0] = dat;
        break;

    case SDMMC_RESP_LONG:
        for(int i = 0; i < 4; ++i) {
            dat = res << 24;
            res = REG_MSC_RES(d->msc_nr);
            dat |= res << 8;
            res = REG_MSC_RES(d->msc_nr);
            dat |= res >> 8;
            data[i] = dat;
        }

        break;

    default:
        return;
    }

    /* The response FIFO is drained even when the caller does not want the
     * data, otherwise it would be left for the next command to read. */
    if(d->resp)
        memcpy(d->resp->data, data, sizeof(data));
}

static void msc_interrupt(msc_drv* d)
{
    const unsigned tmo_bits = jz_orm(MSC_IFLAG, TIME_OUT_READ, TIME_OUT_RES);
    const unsigned crc_bits = jz_orm(MSC_IFLAG, CRC_RES_ERROR,
                                     CRC_READ_ERROR, CRC_WRITE_ERROR);
    const unsigned err_bits = tmo_bits | crc_bits;

    /* An interrupt can still arrive after the request was aborted */
    if(!d->req_running)
        return;

    unsigned iflag = REG_MSC_IFLAG(d->msc_nr) & ~REG_MSC_IMASK(d->msc_nr);
    bool handled = false;

    /* In case card was removed */
    if(!msc_card_detect(d)) {
        msc_finish_request(d, SDMMC_STATUS_ERROR);
        return;
    }

    /* Check for errors */
    if(iflag & err_bits) {
        int st;
        if(iflag & crc_bits)
            st = SDMMC_STATUS_INVALID_CRC;
        else
            st = SDMMC_STATUS_TIMEOUT;

        msc_finish_request(d, st);
        return;
    }

    /* Read the command response */
    if(iflag & BM_MSC_IFLAG_END_CMD_RES) {
        msc_read_response(d);
        jz_writef(MSC_IMASK(d->msc_nr), END_CMD_RES(1));
        jz_overwritef(MSC_IFLAG(d->msc_nr), END_CMD_RES(1));
        handled = true;
    }

    /* Check if the "done" interrupt is signaled */
    if(iflag & d->iflag_done) {
        /* Discard after DMA in case of hardware cache prefetching.
         * Only needed for read operations.
         */
        if(d->data_present && !d->data_write)
            discard_dcache_range(d->data_buf, d->data_len);

        msc_finish_request(d, SDMMC_STATUS_OK);
        return;
    }

    if(!handled) {
        panicf("msc%d: irq bug! iflag:%08x raw_iflag:%08lx imask:%08lx",
               d->msc_nr, iflag, REG_MSC_IFLAG(d->msc_nr),
               REG_MSC_IMASK(d->msc_nr));
    }
}

/* ---------------------------------------------------------------------------
 * sdmmc_host controller operations
 */

static void msc_set_card_power(msc_drv* d, bool enable)
{
    if(d->config->pwr_gpio == GPIO_NONE)
        return;

    gpio_set_level(d->config->pwr_gpio,
                   enable ? d->config->pwr_active_level
                          : !d->config->pwr_active_level);

    /* Let the rail settle before the card is asked for anything */
    udelay(1000);
#else
    (void)d; (void)enable;
}

static void msc_set_power_enabled(void* controller, bool enable)
{
    msc_drv* d = controller;

    /* A board without a card power switch still gets a controller reset,
     * which sdmmc_host says is good enough. */
    if(enable) {
        msc_set_card_power(d, true);
        msc_ctl_reset(d);
        d->powered = true;
        msc_apply_bus_clock(d);
        d->send_init = true;
    } else {
        d->powered = false;
        msc_ctl_reset(d);
        msc_set_card_power(d, false);
    }
}

static void msc_set_bus_width(void* controller, uint32_t width)
{
    msc_drv* d = controller;

    /* Bus width is controlled per command with MSC_CMDAT. */
    if(width == SDMMC_BUS_WIDTH_8BIT)
        jz_vwritef(d->cmdat_def, MSC_CMDAT, BUS_WIDTH_V(8BIT));
    else if(width == SDMMC_BUS_WIDTH_4BIT)
        jz_vwritef(d->cmdat_def, MSC_CMDAT, BUS_WIDTH_V(4BIT));
    else
        jz_vwritef(d->cmdat_def, MSC_CMDAT, BUS_WIDTH_V(1BIT));
}

static void msc_set_bus_clock(void* controller, uint32_t clock)
{
    msc_drv* d = controller;

    d->bus_clock = clock;

    /* A controller reset clears the timing registers, so a rate requested
     * while the bus is powered down is only recorded here and programmed
     * by set_power_enabled() after the reset. */
    if(d->powered)
        msc_apply_bus_clock(d);
}

static void msc_abort_command(void* controller)
{
    msc_drv* d = controller;
    int irq = disable_irq_save();

    if(d->req_running) {
        logf("msc%d: abort", d->msc_nr);
        msc_finish_request(d, SDMMC_STATUS_ERROR);
    }

    restore_irq(irq);
}

/* `io_abort` sets MSC_CMDAT[IO_ABORT], which tells the controller that this
 * command terminates a data transfer that is still in progress.  A CMD12
 * closing a transfer which ran to completion does not need it; PM 26.8.8 and
 * 26.8.9 only call for it when stopping early.
 */
static int msc_do_command(msc_drv* d,
                          const struct sdmmc_host_command* cmd,
                          struct sdmmc_host_response* resp,
                          bool io_abort)
{
    unsigned cmdat = d->cmdat_def;
    unsigned iflag_done = jz_orm(MSC_IFLAG, END_CMD_RES);
    bool has_data = SDMMC_DATA_PRESENT(cmd->flags);
    bool is_write = SDMMC_DATA_DIR(cmd->flags) == SDMMC_DATA_WRITE;
    unsigned data_len = cmd->nr_blocks * cmd->block_len;

    /* RESP_FMT takes the SD response format number. R1, R6 and R7 are all
     * 48-bit responses covered by a CRC and use format 1; R3 is the 48-bit
     * response without one.
     */
    switch(SDMMC_RESP_LENGTH(cmd->flags)) {
    case SDMMC_RESP_NONE:
        break;

    case SDMMC_RESP_SHORT:
        if(cmd->flags & SDMMC_RESP_NOCRC)
            cmdat |= jz_orf(MSC_CMDAT, RESP_FMT(3));
        else
            cmdat |= jz_orf(MSC_CMDAT, RESP_FMT(1));
        break;

    case SDMMC_RESP_LONG:
        cmdat |= jz_orf(MSC_CMDAT, RESP_FMT(2));
        break;

    default:
        panicf("%s: bad resp mode", __func__);
    }

    if(cmd->flags & SDMMC_RESP_BUSY)
        cmdat |= jz_orm(MSC_CMDAT, BUSY);

    if(d->send_init) {
        cmdat |= jz_orm(MSC_CMDAT, INIT);
        d->send_init = false;
    }

    if(io_abort)
        cmdat |= jz_orm(MSC_CMDAT, IO_ABORT);

    if(has_data) {
        cmdat |= jz_orm(MSC_CMDAT, DATA_EN);
        if(is_write) {
            cmdat |= jz_orm(MSC_CMDAT, WRITE_READ);
            iflag_done = jz_orm(MSC_IFLAG, WR_ALL_DONE);
        } else {
            iflag_done = jz_orm(MSC_IFLAG, DMA_DATA_DONE);
        }
    }

    unsigned imask = jz_orm(MSC_IMASK,
                            CRC_RES_ERROR, CRC_READ_ERROR, CRC_WRITE_ERROR,
                            TIME_OUT_RES, TIME_OUT_READ, END_CMD_RES);
    imask |= iflag_done;

    /* Set up the state the interrupt handler reads before the controller is
     * programmed, so it always sees a consistent request. */
    d->resp = resp;
    d->resp_length = SDMMC_RESP_LENGTH(cmd->flags);
    d->data_buf = cmd->buffer;
    d->data_len = data_len;
    d->data_present = has_data;
    d->data_write = is_write;
    d->iflag_done = iflag_done;
    d->status = SDMMC_STATUS_OK;

    /* Program the controller */
    if(has_data) {
        REG_MSC_NOB(d->msc_nr) = cmd->nr_blocks;
        REG_MSC_BLKLEN(d->msc_nr) = cmd->block_len;
    }

    REG_MSC_CMD(d->msc_nr) = cmd->command;
    REG_MSC_ARG(d->msc_nr) = cmd->argument;
    REG_MSC_CMDAT(d->msc_nr) = cmdat;

    REG_MSC_IFLAG(d->msc_nr) = imask;
    REG_MSC_IMASK(d->msc_nr) &= ~imask;

    if(has_data) {
        d->dma_desc.nda = 0;
        d->dma_desc.mem = PHYSADDR(cmd->buffer);
        d->dma_desc.len = data_len;
        d->dma_desc.cmd = 2; /* ID=0, ENDI=1, LINK=0 */
        commit_dcache_range(&d->dma_desc, sizeof(d->dma_desc));

        if(is_write)
            commit_dcache_range(cmd->buffer, data_len);
        else
            discard_dcache_range(cmd->buffer, data_len);

        /* Unaligned address for DMA doesn't seem to work correctly.
         * FAT FS driver should ensure proper alignment of all buffers,
         * so in practice this panic should not occur, but if it does
         * I want to hear about it. */
        if(UNLIKELY(d->dma_desc.mem & 3)) {
            panicf("msc%d bad align: %08x", d->msc_nr,
                   (unsigned)d->dma_desc.mem);
        }

        jz_writef(MSC_DMAC(d->msc_nr), MODE_SEL(0), INCR(0), DMASEL(0));
        REG_MSC_DMANDA(d->msc_nr) = PHYSADDR(&d->dma_desc);
    }

    /* Begin processing */
    d->req_running = 1;
    msc_led_trigger();
    jz_writef(MSC_CTRL(d->msc_nr), START_OP(1));
    if(has_data)
        jz_writef(MSC_DMAC(d->msc_nr), ENABLE(1));

    timeout_register(&d->cmd_tmo, msc_req_timeout, COMMAND_TIMEOUT,
                     (intptr_t)d);
    semaphore_wait(&d->cmd_done, TIMEOUT_BLOCK);

    int status = d->status;
    if(status != SDMMC_STATUS_OK) {
        logf("msc%d: err:%d, cmd%d, arg:%x", d->msc_nr, status,
             cmd->command, cmd->argument);

        /* The transfer was left in progress, so it has to be stopped as the
         * SD spec requires; the controller will not do it for us.  There is
         * no point trying if the card is the thing that went away.
         *
         * Recursion depth is limited to one because CMD12 carries no data.
         */
        if(has_data && msc_card_detect(d)) {
            static const struct sdmmc_host_command cmd12 = {
                .command = SD_STOP_TRANSMISSION,
                .flags   = SDMMC_RESP_SHORT | SDMMC_RESP_BUSY,
            };

            msc_do_command(d, &cmd12, NULL, true);
        }
    }

    return status;
}

static int msc_submit_command(void* controller,
                              const struct sdmmc_host_command* cmd,
                              struct sdmmc_host_response* resp)
{
    return msc_do_command(controller, cmd, resp, false);
}

static const struct sdmmc_controller_ops msc_ops = {
    .set_power_enabled = msc_set_power_enabled,
    .set_bus_width     = msc_set_bus_width,
    .set_bus_clock     = msc_set_bus_clock,
    .submit_command    = msc_submit_command,
    .abort_command     = msc_abort_command,
};

/* ---------------------------------------------------------------------------
 * Interrupt handlers
 */

static int msc_cd_callback(struct timeout* tmo)
{
    msc_drv* d = (msc_drv*)tmo->data;
    int now_present = msc_card_detect(d) ? 1 : 0;

    /* If the CD pin level changed during the timeout interval, then the
     * signal is not yet stable and we need to wait longer. */
    if(now_present != d->card_present_last) {
        d->card_present_last = now_present;
        return DEBOUNCE_TIME;
    }

    /* If there is a change, then tell sdmmc_host about it */
    if(now_present != d->card_present) {
        d->card_present = now_present;

        /* Don't leave a command waiting on a card which is gone; sdmmc_host
         * aborts too, but only once the storage thread picks the event up. */
        if(!now_present)
            msc_abort_command(d);

        sdmmc_host_set_medium_present(&d->host, now_present != 0);
    }

    return 0;
}

static void msc_cd_interrupt(msc_drv* d)
{
    /* Timer to debounce input */
    d->card_present_last = msc_card_detect(d) ? 1 : 0;
    timeout_register(&d->cd_tmo, msc_cd_callback, DEBOUNCE_TIME, (intptr_t)d);

    /* Invert the IRQ */
    gpio_flip_edge_irq(d->config->cd_gpio);
}

void MSC0(void)
{
    msc_interrupt(&msc_drivers[0]);
}

void MSC1(void)
{
    msc_interrupt(&msc_drivers[1]);
}

static void msc0_cd_interrupt(void)
{
    msc_cd_interrupt(&msc_drivers[0]);
}

static void msc1_cd_interrupt(void)
{
    msc_cd_interrupt(&msc_drivers[1]);
}

/* ---------------------------------------------------------------------------
 * Initialization
 */

static void msc_init_one(msc_drv* d, int msc)
{
    /* Lookup config */
    d->config = msc_lookup_config(msc);
    if(!d->config) {
        d->msc_nr = -1;
        return;
    }

    /* Initialize driver state */
    d->msc_nr = msc;
    d->cmdat_def = jz_orf(MSC_CMDAT, RTRG_V(GE32), TTRG_V(LE32));
    d->bus_clock = SDMMC_BUS_CLOCK_400KHZ;
    d->card_present = 1;
    d->card_present_last = 1;
    semaphore_init(&d->cmd_done, 1, 0);

    /* Start with the card unpowered, matching sdmmc_host's initial state;
     * it powers the bus on when it first wants the card. */
    if(d->config->pwr_gpio != GPIO_NONE)
        gpio_set_function(d->config->pwr_gpio,
                          GPIOF_OUTPUT(!d->config->pwr_active_level));

    if(d->config->pin_mask != 0)
        gpioz_configure(d->config->pin_port, d->config->pin_mask,
                        d->config->pin_func);

    /* Ensure correct clock source */
    msc_soc_init_clock(msc);

    /* Initialize the hardware */
    msc_gate_clock(msc, false);
    msc_ctl_reset(d);
    system_enable_irq(msc == 0 ? IRQ_MSC0 : IRQ_MSC1);

    /* Setup the card detect IRQ */
    if(d->config->cd_gpio != GPIO_NONE) {
        if(gpio_get_level(d->config->cd_gpio) != d->config->cd_active_level) {
            d->card_present = 0;
            d->card_present_last = 0;
        }

        system_set_irq_handler(GPIO_TO_IRQ(d->config->cd_gpio),
                               msc == 0 ? msc0_cd_interrupt : msc1_cd_interrupt);
        gpio_set_function(d->config->cd_gpio, GPIOF_IRQ_EDGE(1));
        gpio_flip_edge_irq(d->config->cd_gpio);
        gpio_enable_irq(d->config->cd_gpio);
    }

    struct sdmmc_host_config host_config = {
        .type          = d->config->storage_type,
        .bus_voltages  = d->config->bus_voltages,
        .bus_widths    = d->config->bus_widths,
        .bus_clocks    = d->config->bus_clocks,
        .max_nr_blocks = MSC_MAX_BLOCKS,
        .is_removable  = d->config->cd_gpio != GPIO_NONE,
    };

    sdmmc_host_init(&d->host, &host_config, &msc_ops, d);
    if(host_config.is_removable)
        sdmmc_host_init_medium_present(&d->host, d->card_present != 0);
}

void sdmmc_host_target_init(void)
{
    for(int i = 0; i < MSC_COUNT; ++i)
        msc_init_one(&msc_drivers[i], i);
}
