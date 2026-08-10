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
 * Based on firmware/target/mips/ingenic_x1000/sfc-x1000.h,
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

#ifndef __SFC_X1600_H__
#define __SFC_X1600_H__

/* Register definitions first; the shared contract follows. */
#include "x1600/sfc.h"
#include "sfc-ingenic.h"

/* X1600 SFC ("v2") vs the X1000's ("v1"). Three differences, all handled in
 * sfc-x1600.c:
 *
 *  1. TRAN_CONF[31:29] is CLK_MODE (SDR only); the line-count mode moved to the
 *     per-phase TRAN_CFG1_n.TRAN_MD at [7:4] (PM 5.7.3.9), keeping the X1000's
 *     enumeration -- which is why SFC_TMODE_* is shared. SFC_CMD() parks the
 *     mode in [31:29] as a carrier and sfc_exec() moves it, so the signature
 *     matches the X1000's. Cross-checked against the vendor SPL.
 *  2. The clock divider moved from the shared CPM_SSICDR to CPM_SFCCDR
 *     (PM 11.1.2.10), which is what sfc_set_clock() drives.
 *  3. GLB0 gains CDT_EN and DES_EN; both are cleared in favour of the
 *     v1-compatible register set. NOT IMPLEMENTED: CDT mode.
 *
 * sfc_irq_begin()/sfc_irq_end() are no-ops here: no DMA mode, no IRQ_SFC yet. */

/* Carrier bits for the mode inside SFC_CMD()'s result -- NOT written to
 * TRAN_CFG0. They sit at [31:29] only to match the X1000's layout. */
#define SFC_CMD_TMODE_BP        29
#define SFC_CMD_TMODE_BM        0xe0000000
#define SFC_CMD_TMODE(v)        (((uint32_t)(v) & 0x7) << SFC_CMD_TMODE_BP)
#define SFC_CMD_GET_TMODE(c)    (((c) & SFC_CMD_TMODE_BM) >> SFC_CMD_TMODE_BP)

/* Fields x1600/sfc.h declares registers for but not fields:
 * TRAN_CFG1.TRAN_MD is PM 5.7.3.9, the GLB1 bits PM 5.7.3.2. */
#define SFC_TRAN_CONF1_TRAN_MD_BP   4
#define SFC_TRAN_CONF1_TRAN_MD_BM   0xf0
#define SFC_TRAN_CONF1_TRAN_MD(v)   (((uint32_t)(v) & 0xf) << SFC_TRAN_CONF1_TRAN_MD_BP)
#define SFC_GLB1_CHIP1_SEL_BM       0x80000000  /* 1 = force select chip1 */
#define SFC_GLB1_CHIP_SEL_BM        0x00000003  /* 00 = chip 0 (default) */

/** \brief Macro to generate an SFC command for use with sfc_exec()
 * \param cmd       Command number (up to 16 bits)
 * \param tmode     SPI transfer mode (SFC_TMODE_*)
 * \param awidth    Number of address bytes
 * \param dwidth    Number of dummy cycles (1 cycle = 1 bit)
 * \param pfmt      Phase format (address first or dummy first)
 * \param data_en   1 to enable data phase, 0 to omit it
 */
#define SFC_CMD(cmd, tmode, awidth, dwidth, pfmt, data_en)  \
    (jz_orf(SFC_TRAN_CONF, COMMAND(cmd), CMD_EN(1),         \
            ADDR_WIDTH(awidth), DUMMY_BITS(dwidth),         \
            PHASE_FMT(pfmt), DATA_EN(data_en))              \
     | SFC_CMD_TMODE(tmode))

/* Mux PC17..PC22 to the SFC, function 0 (PM Table 21-4).  ⚠ NOT the X1000's
 * PA26-PA31: those carry TFT data here, so its pin numbers would break the
 * display rather than the flash.  Only the SPL needs this -- gpio_init()
 * applies PINGROUP_SFC for the main firmware.  Idempotent. */

/* X1600-only: the X1000's SFC has no CDT mode.  Not a register, so not in
 * x1600/ -- those headers are generated from utils/reggen-ng/x1600.reggen. */
#define SFC_CDT_WORDS   256

#endif /* __SFC_X1600_H__ */
