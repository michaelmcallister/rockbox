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

#ifndef __SPL_X1600_H__
#define __SPL_X1600_H__

#include <stddef.h>
#include <stdint.h>

/* Where the Rockbox bootloader lives in SPI-NAND, and how much of it to read.
 * 0x6800 is the largest legal X1600 SPL image: 512 B signature + 1536 B SC_Boot
 * key + the PM's 24 KiB text cap.
 *
 * Taken from this device's own flash rather than the X1000's: the window is
 * erased, and it clears the NAND partition table at
 * 0x5800 -- which must survive, since the vendor firmware needs it to find its
 * own partitions -- by 2 KiB. Both the SPL and the start of the bootloader sit
 * in the same 128 KiB erase block, so an installer must read-modify-write it
 * rather than erase blindly. Full layout in docs/hibyr1-native-bringup.md. */
#define SPL_BOOT_STORAGE_ADDR   0x6800
#define SPL_BOOT_STORAGE_SIZE   (102 * 1024)

#include "spl-ingenic.h"

#endif /* __SPL_X1600_H__ */
