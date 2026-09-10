# HiBy R1 native builds

These are development builds of the native X1600 port. The native bootloader
replaces the vendor SPL and does not provide dual boot. Building successfully
does not establish that a new revision has been tested on hardware.

## Download

Open the **HiBy R1** run in this repository's **Actions** tab. Download the
artifact for your computer and extract the TAR.GZ file inside it:

| Artifact | Computer |
| --- | --- |
| `hibyr1-macos-arm64` | Apple Silicon Mac |
| `hibyr1-macos-x86_64` | Intel Mac |
| `hibyr1-linux-x86_64` | 64-bit Intel/AMD Linux |

Each package contains:

- `rockbox-hibyr1.zip`: the complete `.rockbox` directory, including firmware,
  codecs, plugins and fonts.
- `rockbox.r1`: a separate copy of the same application firmware.
- `bootloader.r1`: the bootloader, flash SPL and USB recovery stage in one package.
- `jztool`: the USB recovery utility for the selected computer, with libusb linked in.
- `REVISION.txt`, `SHA256SUMS`, licenses and the exact Rockbox and libusb sources.

The `hibyr1-symbols` artifact contains ELF files and maps for diagnosing crashes.
User packages and symbols are retained for 14 days. A push to `x1600-native`
starts a new build.

Check the extracted files from a terminal in the package directory:

```sh
# macOS
shasum -a 256 -c SHA256SUMS
# Linux
sha256sum -c SHA256SUMS
```

## Update an existing native installation

These builds use model ID 126. Earlier development builds used model ID 125;
their bootloader cannot load the new firmware. For those installations, follow
the bootloader update procedure below using the matching `bootloader.r1`.
Until it is installed, jztool can boot that loader from RAM.

Extract `rockbox-hibyr1.zip` onto the root of the FAT32 microSD card, merging
the `.rockbox` directory. Safely eject the card/device, disconnect USB and restart
the R1. This updates the application, codecs and plugins together. It does not
require flashing the bootloader.

Use the full ZIP when updating between revisions: copying only `rockbox.r1`
can leave incompatible plugins or codecs behind.

## Install or update the native bootloader

1. Prepare a FAT32 microSD card with `rockbox-hibyr1.zip` extracted at its root.
   Copy `bootloader.r1` to the card root as well. Keep a copy of `bootloader.r1`
   beside `jztool` on the computer.
2. Safely eject the card and insert it into the R1 before starting recovery.
3. Turn the R1 fully off and unplug USB. Hold **NEXT**, the lower button on
   the right, while plugging USB into the computer. Do not press POWER.
   Release NEXT after about three seconds. The display may remain blank;
   the USB device ID is `a108:eaef`.
4. From a terminal in the package directory, run:

   ```sh
   # macOS
   ./jztool hibyr1 load bootloader.r1
   # Linux (raw USB access normally requires root)
   sudo ./jztool hibyr1 load bootloader.r1
   ```

   This loads recovery into RAM. It does not write flash. No separate libusb
   installation is required. On macOS, allow the downloaded executable in
   System Settings if Gatekeeper requests approval.
5. If recovery enters USB mode, safely eject the R1 on the computer and
   disconnect USB to return to the menu. Use **VOL+ / VOL-** to move,
   **PLAY** to select and **POWER** to cancel.
6. Before the first native installation, select **Backup bootloader**. Keep
   the resulting `hibyr1-boot.bin` safe and copy it off the card before flashing.
   A backup made after installing Rockbox contains Rockbox, not the vendor SPL.
7. Select **Install bootloader**, read the on-screen confirmation and press
   PLAY to proceed. Wait for the result before powering off. Exit recovery
   and restart with the prepared card inserted.

To restore a saved bootloader, place that device's `hibyr1-boot.bin` at the
card root, enter recovery with jztool and select **Restore bootloader**. Do not
use a backup from a different device. Keep the USB recovery package available.

## Build locally

Firmware is built on Linux using Rockbox's `mipsel-elf` GCC 9.5.0 toolchain.
The workflow builds it with `tools/rockboxdev.sh --target=i` and caches the
installed toolchain. It configures the target by name, `hibyr1native`, to avoid
confusing it with the hosted R1 target or a reused numeric target ID.

With the cross-compiler on PATH and the workflow's build dependencies installed:

```sh
bash .github/scripts/build-hibyr1.sh /tmp/r1-build /tmp/r1-output
bash .github/scripts/build-jztool.sh /tmp/jztool-build /tmp/jztool-output
```

Use fresh build directories. The firmware script builds the full ZIP and all
three bootloader stages, then checks the application model/checksum, the ZIP
contents and the bootloader package members. The jztool script downloads the
pinned libusb source, checks its SHA-256 and builds a portable binary. It needs
a C compiler, Make, curl, tar, Perl and pkg-config; macOS also needs the Xcode
command-line tools. The bundled `rockbox-source.tar.gz` includes `REVISION.txt`
so these commands also work from the extracted sources without a Git checkout.
A source archive is included in the output when building from Git; it is not
duplicated when rebuilding from an existing source archive.
