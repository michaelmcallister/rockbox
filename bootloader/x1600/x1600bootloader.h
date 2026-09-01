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

#ifndef __X1600BOOTLOADER_H__
#define __X1600BOOTLOADER_H__

#include "config.h"
#include "button.h"
#include <stdbool.h>
#include <stdint.h>

/* Five keys and no more: POWER, NEXT, PLAY, VOL-, VOL+. No PREV, so VOL+/VOL-
 * move through lists. PLAY and the volume keys sit on a SADC resistor ladder,
 * so the SADC driver has to be running before a key can be read.
 *
 * NEXT is the recovery key because it is also the BOOT_SEL1 strap -- already the
 * key held to enter BootROM USB mode. */

/* Keymap, chosen to fit the R1's five keys */
#define BL_RECOVERY         BUTTON_NEXT     /* also the BootROM USB strap */
#define BL_UP               BUTTON_VOL_UP
#define BL_DOWN             BUTTON_VOL_DOWN
#define BL_SELECT           BUTTON_PLAY
#define BL_QUIT             BUTTON_POWER
#define BL_UP_NAME          "VOL+"
#define BL_DOWN_NAME        "VOL-"
#define BL_SELECT_NAME      "PLAY"
#define BL_QUIT_NAME        "POWER"

/* No screenshot key: the X1000 bootloader binds one, the R1 has no spare key */

/* No dual boot into the original firmware. Installing Rockbox leaves the OF
 * partitions untouched -- only block 0 is written -- so it remains possible;
 * the R1's OF is Linux and mtd5 holds the A/B slot flag. */
#define BOOTBACKUP_FILE     "/hibyr1-boot.bin"

/* A/B slot layout. Sizes verified, offsets derived -- see the block comment. */
#define R1_MTD_UBOOT_ADDR       0x00000000
#define R1_MTD_UBOOT_LENGTH     (512 * 1024)
#define R1_MTD_KERNEL_ADDR      0x00080000
#define R1_MTD_KERNEL_LENGTH    (5 * 1024 * 1024)
#define R1_MTD_KERNEL2_ADDR     0x03280000
#define R1_MTD_KERNEL2_LENGTH   (5 * 1024 * 1024)
#define R1_MTD_OTA_ADDR         0x04f80000
#define R1_MTD_OTA_LENGTH       (512 * 1024)

#define R1_OTA_SLOT_A           "ota:kernel"
#define R1_OTA_SLOT_B           "ota:kernel2"


/* Split as bootloader/x1000/ is; the screen primitives are shared with it in
 * bootloader/ingenic/gui-ingenic.h. */
#include <stdint.h>

enum bl_stage {
    BL_ST_ENTRY = 1, BL_ST_SYSTEM, BL_ST_ALLOC, BL_ST_KERNEL, BL_ST_I2C,
    BL_ST_POWER, BL_ST_BUTTON, BL_ST_IRQ, BL_ST_STORAGE, BL_ST_STORAGE_FAIL,
    BL_ST_FILESYSTEM, BL_ST_USB, BL_ST_MOUNT, BL_ST_BTN, BL_ST_BOOT,
    BL_ST_BOOT_RETURNED,
    BL_ST_BOOT_NOMEM,      /* core_alloc_maximum() failed        */
    BL_ST_BOOT_LOADFAIL,   /* load_firmware() could not read it  */
    BL_ST_BOOT_JUMP,       /* about to jump into Rockbox         */
    BL_ST_RECOVERY,        /* BL_RECOVERY held: install path     */
    BL_ST_INSTALL_OK,      /* install_bootloader() succeeded     */
    BL_ST_INSTALL_FAIL,    /* install_bootloader() failed        */
    BL_ST_INSTALL_CANCELLED, /* user declined at the prompt      */

    /* Display path, split finely enough that one boot names the failing step */
    BL_ST_LCD_INIT,        /* lcd_init() returned                */
    BL_ST_LCD_FIRST_UPD,   /* first lcd_update() returned        */
    BL_ST_BACKLIGHT,       /* backlight_init() returned          */
    BL_ST_MENU_DRAWN,      /* recovery menu's first frame pushed */
    BL_ST_MENU_INPUT,      /* waiting for a button              */
    BL_ST_LCDTEST,         /* bring-up test pattern pushed      */
    BL_ST_ADC,             /* adc_init() returned               */
};

/* beacon.c. The payload variables are written by whichever file learns the
 * fact, and read back by bl_beacon() when it publishes. */
void bl_beacon(uint32_t stage);

extern volatile uint32_t bl_tick_probe;
extern volatile int32_t  bl_mounts;
extern volatile int32_t  bl_sd_state;
extern volatile int32_t  bl_pmu_en;

/* gui.c -- panel bring-up.  The screen primitives live in
 * bootloader/ingenic/gui-ingenic.c, shared with the X1000. */
void init_lcd(void);
void gui_shutdown(void);
extern bool lcd_ready;

/* utils.c -- buttons, prompts, card presence, cache probes */
void wait_btn_release(void);
int  wait_btn_press(void);
bool confirm(const char* what, const char* consequence);
bool card_available(void);

/* A Rockbox image placed directly in DRAM by the host rather than read from a
 * filesystem, so the boot path can be exercised with no card present.
 *
 * 0x82000000 is 32 MiB in: clear of the bootloader and of where Rockbox is
 * copied to, with the header one page below. Read through the UNCACHED alias --
 * the host wrote it straight to DRAM, so a cached read can hit a stale line.
 * Writes nothing to flash. */
#define BL_PRELOAD_HDR    0xa1fff000u
#define BL_PRELOAD_IMAGE  0xa2000000u
#define BL_PRELOAD_MAGIC  0x5230424Fu   /* "R0BO" little-endian-ish tag */

/* boot.c -- load Rockbox and hand control to it */
int  bl_preloaded_size(void);
void boot_rockbox(void);

/* recovery.c -- install / backup / restore */
void run_action(int which);
void recovery_menu(void);

#endif /* __X1600BOOTLOADER_H__ */
