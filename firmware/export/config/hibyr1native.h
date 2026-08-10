/*
 * Rockbox target configuration for the HiBy R1, native (bare metal) port.
 *
 * SoC: Ingenic X1600E - XBurst1, MIPS32r2, single core, 32-byte cache lines,
 *      16 KiB I-cache + 16 KiB D-cache (CP0 Config1 = 0xBE27139B, read by our
 *      own bare-metal code on the device), 64 MiB on-package DRAM at
 *      0x00000000-0x03FFFFFF.
 *
 * Every value in this file is either measured on the device or taken from the
 * X1600 Programming Manual. Anything not yet established is marked with an
 * explicit TODO(x1600) - please do not "fill in" such a value by analogy with
 * the X1000, the two SoCs differ in more places than they agree.
 */

/* RoLo-related defines */
#define MODEL_NAME      "HiBy R1 Native"
#define MODEL_NUMBER    125
#define BOOTFILE_EXT    "r1"
#define BOOTFILE        "rockbox." BOOTFILE_EXT
#define BOOTDIR         "/.rockbox"

/* CPU defines */
#define CONFIG_CPU          X1600

/* 24 MHz crystal. The BootROM leaves CPAPCR = 0x034051CD, which is
 * M=52 N=1 OD1=2, i.e. 24 MHz * 52 / (1 * 2) = 624 MHz - exactly the boot
 * frequency the PM documents, so EXCLK really is 24 MHz. */
#define X1600_EXCLK_FREQ    24000000

/* The vendor Linux kernel runs APLL at 1104 MHz (CPAPCR = 0x02E049CD read live
 * from the running device), so it is known-good at this silicon's default core
 * voltage. clk_init() programs APLL and CPCCR to match.
 * The CPCCR decode reads 1,104,000,000 Hz and an independent CP0 Count
 * measurement puts the core at exactly half that. Note the core runs *much*
 * slower than 624 MHz
 * straight out of the BootROM (measured: a 6.04e9-iteration loop took over 10
 * minutes, consistent with running directly off the 24 MHz crystal), so no code
 * may assume CPU_FREQ before the clock driver has run. */
#define CPU_FREQ            1104000000

#ifndef SIMULATOR
/* The X1000 port clocks the OST from EXCLK with PRESCALE = /1 and the X1600
 * OST (SYS_OST at 0x12000000) has the same OSTCCR PRESCALE encoding
 * (PM sec 14.3.2.1: 00 = "Internal clock: CLK/1"). The PM does not name the
 * source clock, however.
 * Timed against an external reference, which is the only way: measuring the OST
 * against anything it also drives is circular. 5,989,878 Hz over 211 s,
 * against the 6,000,000 Hz that EXCLK/4 predicts. 0.17% apart, which is the
 * host's one-second timestamp granularity, not drift. */
#define TIMER_FREQ          X1600_EXCLK_FREQ
#endif

/* Kernel defines */
#define INCLUDE_TIMEOUT_API
#define HAVE_SEMAPHORE_OBJECTS

/* Drivers
 *
 * The R1 has two hardware I2C controllers plus one bit-banged bus:
 *   I2C0 (0x10050000, PA28/PA29) - AXP2101 PMU @ 0x34, CW2015 gauge @ 0x62
 *   I2C1 (0x10051000, PB19/PB20) - CST8xx touchscreen @ 0x15
 *   bit-banged (PB30/PB31)       - CS43131 DAC @ 0x30 (no controller here)
 * All three pin assignments verified from /sys/kernel/debug/gpio labels. */
#define HAVE_I2C_ASYNC

/* Buffer for plugins and codecs. */
#define PLUGIN_BUFFER_SIZE  0x200000 /* 2 MiB */
#define CODEC_SIZE          0x100000 /* 1 MiB */

/* LCD defines
 *
 * 480x800 RGB666 parallel TFT. Panel controller is a Sitronix ST7701S (the
 * vendor kernel module name "lcd_lg35583" is misleading); it is driven by the
 * X1600 DPU at 0x13050000, which is a different IP block from the X1000 LCDC -
 * none of lcd-x1000.c applies.
 *
 * Timings verified from FBIOGET_VSCREENINFO and from the live DPU registers:
 *   pixel clock 28,303,248 Hz
 *   left 24, right 24, upper 8, lower 14, hsync 24, vsync 5
 *   -> HTOTAL 552, VTOTAL 827, ~62 Hz refresh
 */
#define CONFIG_LCD          LCD_HIBY_R1
#define LCD_WIDTH           480
#define LCD_HEIGHT          800
#define LCD_DEPTH           16
#define LCD_PIXELFORMAT     RGB565
#define LCD_DPI             233 /* sqrt(480^2 + 800^2) / 4.0" diagonal */
#define HAVE_LCD_COLOR
#define HAVE_LCD_BITMAP

/* The DPU is clock-gated *and* its power domain (pd_mem_dpu) is switched off
 * when the panel sleeps: with it asleep every DPU register reads back 0x80,
 * so a real enable/disable is possible. */
#define HAVE_LCD_ENABLE

/* NOT IMPLEMENTED: HAVE_LCD_SLEEP. Achievable -- the power domain can be
 * dropped entirely, and dpu-x1600.c already carries lcd_sleep() -- but it needs
 * a working resume path first. */

/* Backlight defines
 *
 * PWM0 on PC00 (verified from the gpio labels), PWM block at 0x134c0000.
 *
 * The backlight is ON/OFF: pwm-x1600.c is stubs, so the pin is driven as a
 * plain GPIO. No HAVE_BACKLIGHT_BRIGHTNESS, and no software fading either --
 * BACKLIGHT_FADING_SW_SETTING fades by ramping the brightness setting, which on
 * binary hardware is a delayed on/off.
 *
 * TODO(x1600): implement pwm-x1600.c, then restore both. CLKGR1 resets with the
 * PWM bit SET and nothing ungates it, so its registers read garbage with writes
 * swallowed; the pad must return to GPIOF_DEVICE(0) in the same change, or the
 * PWM drives a pin the GPIO block still owns. */
#define HAVE_BACKLIGHT
#define CONFIG_BACKLIGHT_FADING BACKLIGHT_NO_FADING

/* Codec / audio hardware defines
 *
 * Cirrus Logic CS43131, controlled over the bit-banged I2C bus on PB30/PB31 at
 * address 0x30. Power on PB02, reset on PB21, both active high. The part is
 * effectively write-only (reads return 0x00) so the driver keeps a shadow.
 *
 * The AIC (I2S) is at 0x10079000 on the X1600, *not* 0x10020000 as on the
 * X1000. The X1600 AIC has no internal codec, so drivers/audio/x1000-codec.c
 * is dead code here.
 *
 * Only 44.1 kHz and 48 kHz are claimed: those are the only two I2S MCLK
 * divider settings (CPM + 0x7c) that have actually been measured on hardware
 *   44.1k -> 0x74F01163 (M=335, N=4451) -> 22,579,195 Hz, -0.2 ppm
 *   48k   -> 0x70000C35 (M=256, N=3125) -> 24,576,000 Hz, exact
 * with I2SDIV = 8 giving a 64fs BCLK in both cases.
 * NOT IMPLEMENTED: rates above 48k. The CS43131 and the EPLL-derived divider
 * can certainly go higher; extend HW_SAMPR_CAPS once more dividers have been
 * measured. */
#define HAVE_CS43131
#define HW_SAMPR_CAPS   (SAMPR_CAP_44 | SAMPR_CAP_48)

/* The CS43131 has hardware volume, but no tone controls. */
#define HAVE_SW_TONE_CONTROLS

/* Button defines
 *
 * Five physical keys, two on GPIO and three on a SADC resistor ladder; the pin
 * assignments and the ladder voltages are in hibyr1/button-target.h. There is
 * no PREV key, so the keymap navigates with the touchscreen.
 *
 * The X1000 port has no SADC driver at all -- adc-target.h is empty in every
 * X1000 device directory -- so sadc-x1600.c is new code rather than a port. */
/* Reuse the pad the shipped HiBy R1 port already uses. The pad constant only
 * selects which apps/keymaps/keymap-*.c is compiled -- the button bits come
 * from hibyr1/button-target.h either way -- and roughly forty plugins know
 * this one, where an unfamiliar pad makes every one of them fail to build.
 * See the note in button-target.h. */
#define CONFIG_KEYPAD   HIBY_R3PROII_PAD
#define HAVE_TOUCHSCREEN
#define HAVE_BUTTON_DATA

/* CST8xx capacitive controller on I2C1 @ 0x15, IRQ on PA16, reset on PA17,
 * rail is the AXP2101's aldo2. Single touch only, 480x800, no axis swap or
 * flip. Torn reads come back as all-0xff and decode to a bogus
 * "finger=15, x=4095, y=4095" - the driver must either use the PA16 IRQ or
 * reject any block with count > 1 || x > 479 || y > 799. */
#define HAVE_CST8XX
#define CST8XX_NUM_POINTS 1

/* SADC channel 1 is headphone detect; in-range is 2800-3300 mV (verified). */
#define HAVE_HEADPHONE_DETECTION

/* Storage defines
 *
 * The only user-accessible storage is the microSD slot, which hangs off MSC1
 * (card detect PB22, card power PC25). Note this is the *reverse* of every
 * X1000 target, where the SD card is on MSC0.
 *
 * MSC0 is wired to the SDIO Wi-Fi module and is not used by Rockbox.
 *
 * The internal SPI-NAND (Winbond W25N01GV on SFC @ 0x13440000) holds the
 * vendor firmware and is not exposed as Rockbox storage.
 *
 * Caveat for users: Rockbox's FAT driver (firmware/common/fat.c) is FAT16/32
 * only, so exFAT-formatted cards - which is how large cards ship - will not
 * mount. */
#define CONFIG_STORAGE  STORAGE_SD
#define HAVE_HOTSWAP
#define HAVE_HOTSWAP_STORAGE_AS_MAIN
#define HAVE_MULTIVOLUME
#define HAVE_FAT16SUPPORT

/* Both of these are carried over from the X1000 MSC driver, where the DMA
 * engine needs aligned, non-cached-boundary-crossing buffers. That requirement
 * has now been confirmed for the X1600 in the PM: the SDMA programming sequence
 * (PM 26.8.2.2, line 27130) says "Step 2: If the address is not word boundary,
 * it is suggested to configure MSC_DMAC.ALIGNEN and MSC_DMAC.AOFST", i.e. the
 * descriptor's data address is expected to be word aligned unless the alignment
 * fixup logic is enabled - which msc-x1600.c does not enable, and instead
 * panics on a misaligned buffer. Keeping both defines is what guarantees the
 * panic never fires.
 *
 * Cache line size is 32 bytes (CP0 Config1 = 0xBE27139B), hence
 * CACHEALIGN_BITS = 5 in firmware/export/x1600.h. */
#define STORAGE_WANTS_ALIGN
#define STORAGE_NEEDS_BOUNCE_BUFFER

/* RTC block at 0x10003000 (verified from /proc/iomem and the device tree).
 * Driven by the shared drivers/rtc/rtc_ingenic.c: the register layout matches
 * the X1000's apart from HWCR.EALM's bit position, which is why that driver
 * takes its register header from the target's ingenic-soc.h. */
#define CONFIG_RTC      RTC_X1600
/* NOT IMPLEMENTED: HAVE_RTC_ALARM */

/* Power management
 *
 * AXP2101 PMU on I2C0 @ 0x34 (IRQ PB23) and a CW2015 fuel gauge on I2C0 @ 0x62
 * (both verified). Rockbox already has drivers for both:
 *   firmware/drivers/axp-2101.c  (also used by the Eros Q native port)
 *   firmware/drivers/cw2015.c    (also used by power-shanlingq1.c)
 * so neither needs writing from scratch.
 *
 * Battery: single-cell Li-ion, 1600 mAh design capacity, Vmax 4.35 V, vendor
 * charge voltage limit 4400 mV, termination current 100 mA. */
#define CONFIG_BATTERY_MEASURE \
    (VOLTAGE_MEASURE|CURRENT_MEASURE|PERCENTAGE_MEASURE|TIME_MEASURE)
#define CONFIG_CHARGING        CHARGING_MONITOR
#define HAVE_SW_POWEROFF

#ifndef SIMULATOR
#define HAVE_AXP2101
/* CW2015 fuel gauge. The vendor's battery profile (1600 mAh, 4.35 V) is
 * recorded in docs/hibyr1-RESUME.md; cw2015.c has no consumer for it yet. */
#define HAVE_CW2015
#define HAVE_POWEROFF_WHILE_CHARGING
#endif

/* Only one battery type */
#define BATTERY_CAPACITY_DEFAULT 1600
#define BATTERY_CAPACITY_MIN     1600
#define BATTERY_CAPACITY_MAX     1600
#define BATTERY_CAPACITY_INC     0

/* Multiboot */
#define HAVE_BOOTDATA
#define BOOT_REDIR "rockbox_main.hiby_r1"

#ifndef SIMULATOR

/* USB: the DesignWare device stack, on by default.
 *
 * This sat behind -DX1600_USB with USB_NONE otherwise, because an app-mode USB
 * attach froze the device once. It was never reproduced, and there is now a
 * mechanism for it that has been fixed: axp2101_adc_read() returns INT_MIN when
 * its I2C read fails, and x1600_usb_refresh_detect() compared that against a
 * millivolt threshold -- so a transient PMU NAK reported USB_EXTRACTED with the
 * cable still connected, tearing the stack down mid-session. Guarded in
 * cb783da15f.
 *
 * Since then, across three sessions: enumeration every time, mass storage reads
 * AND writes verified by md5 from the host, a CDC serial console carrying
 * logf() output, and audio playing throughout with the cable attached.
 *
 *  - USB_DW_TURNAROUND 5 is the Synopsys value for a 16-bit UTMI+ PHY (8-bit
 *    wants 9), and the core reports which it has -- GHWCFG2.HSPHYTYPE says
 *    UTMI+, GHWCFG4.UTMIWIDTH says 8-or-16. Determined, not measured.
 *  - USB_DW_ARCH_SLAVE sidesteps the DMA cache hazard entirely (see
 *    usb-x1600.c) and needs no new CONFIG_CPU arm in usb-designware.c. */

#define CONFIG_USBOTG USBOTG_DESIGNWARE
#define USB_DW_ARCH_SLAVE
#define USB_DW_TURNAROUND 5
#define HAVE_USBSTACK

/* A CDC serial console alongside mass storage. The three-interface composite
 * enumerates on hardware -- MSC (class 8/6/80), CDC ACM (2/2) and CDC data
 * (10) -- and mass storage keeps working alongside it.
 *
 * The console carries logf() output: _logf() calls usb_serial_send() under
 * USB_ENABLE_SERIAL alone. HAVE_SERIAL && LOGF_SERIAL gate a separate path,
 * serial_tx(), for a physical UART */
#define USB_ENABLE_SERIAL
/* The stock firmware's own IDs, as every other Rockbox target uses its
 * manufacturer's (Apple for the iPods, SanDisk for the Sansas, 0x2972 for the
 * M3K). Read out of the OF rootfs: /usr/bin/usb_dev_mass_storage.sh sets
 * 0x32BB/0x0004 with strings "HiBy" / "R1" / "HiBy R1".
 *
 * Presenting the OF's identity is also what makes the device detectable by
 * rbutil. The R1's other gadget ID, 0x18d1/0xd002, is the generic Android
 * composite and is unusable for that -- it matches every Android phone. */
#define USB_VENDOR_ID  0x32BB       /* HiBy */
#define USB_PRODUCT_ID 0x0004       /* R1 */
#define USB_DEVBSS_ATTR __attribute__((aligned(32)))
#define HAVE_USB_POWER
/* Which buttons may flip USB into charge-only mode. The default of 0 means any
 * button held at insertion inverts it, which matters more here than on the
 * X1000 siblings: buttons come from an ADC ladder, so a marginal reading
 * silently disables mass storage while the USB screen still appears. VOL_UP and
 * VOL_DOWN only, as fiiom3k does -- away from the noisy end of the ladder. */
#define USBPOWER_BTN_IGNORE (~(BUTTON_VOL_UP|BUTTON_VOL_DOWN))
#define HAVE_USB_CHARGING_ENABLE
#define HAVE_USB_CHARGING_IN_THREAD
#define TARGET_USB_CHARGING_DEFAULT USB_CHARGING_FORCE
/* The bootloader calls usb_init()/usb_start_monitoring() (as X1000's does), and
 * this is what actually links the stack into it: firmware/SOURCES gates the
 * whole usbstack on
 *     HAVE_USBSTACK && (!BOOTLOADER || HAVE_BOOTLOADER_USB_MODE).
 * Without it the bootloader compiles those calls and fails at link with four
 * undefined references. It earns its place beyond building: a device that
 * cannot boot Rockbox -- no card, an exFAT card, a bad rockbox.r1 -- can still
 * have its card rewritten over USB from the recovery menu, which is the only
 * repair path that does not need a BootROM host session. */
#define HAVE_BOOTLOADER_USB_MODE

/* Modest buffers. The X1000 targets use 128 KiB each; in slave mode the CPU
 * copies every byte, so oversized buffers buy latency rather than throughput,
 * and this player has 64 MB total. */
#define USB_READ_BUFFER_SIZE    (16 * 1024)
#define USB_WRITE_BUFFER_SIZE   (16 * 1024)


/* The SoC-level SADC driver (target/mips/ingenic_x1600/sadc-x1600.c) is present
 * and is authoritative for adc_init()/adc_read()/adc_read_mv().  Defining this
 * compiles out the fallback copies in hibyr1/power-hibyr1.c, which would
 * otherwise be duplicate symbols at link time. */
#define X1600_HAVE_SADC_DRIVER

#endif

/* Rockbox capabilities */
#define HAVE_ALBUMART
#define HAVE_BMP_SCALING
#define HAVE_JPEG
#define HAVE_TAGCACHE
#define HAVE_VOLUME_IN_LIST
#define HAVE_QUICKSCREEN
#define HAVE_HOTKEY
#define AB_REPEAT_ENABLE

/* Deliberately NOT claimed, so nobody wires up a settings entry for them:
 *  - Bluetooth: the R1 has a BT module (PB04 bt_reg_on) but no Rockbox stack.
 *  - Wi-Fi: SDIO module on MSC0, likewise no driver.
 *  - Recording: the X1600 AIC can capture (div_i2s0r exists at CPM + 0x60) but
 *    there is no microphone or line-in on this device.
 *  - HAVE_GENERAL_PURPOSE_LED: PC01 (red) / PC02 (blue) are verified and we
 *    have blinked them, but the led_general_purpose_* glue is not written.
 */
