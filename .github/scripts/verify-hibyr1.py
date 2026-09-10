#!/usr/bin/env python3
"""Reject incomplete bundles and firmware for the hosted R1 target."""

from pathlib import Path
import struct
import sys
import tarfile
import zipfile


def require(condition, message):
    if not condition:
        raise SystemExit(message)


root = Path(sys.argv[1])
revision = (root / "REVISION.txt").read_text().strip()
require(len(revision) >= 10, "Missing source revision")
firmware = (root / "rockbox.r1").read_bytes()
require(len(firmware) > 8 and firmware[4:8] == b"hbr1", "Wrong firmware model")
require(struct.unpack(">I", firmware[:4])[0] == (126 + sum(firmware[8:])) & 0xffffffff,
        "Firmware checksum mismatch")

with zipfile.ZipFile(root / "rockbox-hibyr1.zip") as archive:
    require(archive.testzip() is None, "Corrupt Rockbox ZIP")
    names = archive.namelist()
    require(archive.read(".rockbox/rockbox.r1") == firmware, "ZIP firmware differs")
    info = archive.read(".rockbox/rockbox-info.txt").decode()
    require(f"Version: {revision[:10]}" in info, "Firmware revision mismatch")
    for prefix, suffix in ((".rockbox/codecs/", ".codec"),
                           (".rockbox/rocks/", ".rock"),
                           (".rockbox/fonts/", ".fnt")):
        require(any(name.startswith(prefix) and name.endswith(suffix) for name in names),
                f"Missing {prefix}")

with tarfile.open(root / "bootloader.r1") as archive:
    required = {"spl.r1", "usbstage1.r1", "bootloader2.ucl", "bootloader-info.txt"}
    members = archive.getmembers()
    require(len(members) == len(required) and {m.name for m in members} == required,
            "Unexpected bootloader package members")
    require(all(m.isfile() and m.size > 0 for m in members), "Empty bootloader member")
    info = archive.extractfile("bootloader-info.txt").read().decode()
    require(info.startswith(revision[:10]), "Bootloader revision mismatch")
    stage1 = archive.extractfile("usbstage1.r1").read()
    require(len(stage1) <= 20 * 1024, "USB stage1 exceeds the BootROM limit")
    spl = archive.extractfile("spl.r1").read()
    require(spl[:8] == bytes.fromhex("060504030255aa55"), "Invalid flash SPL signature")
    require(0x800 < len(spl) <= 0x6800, "Invalid flash SPL size")
    require(struct.unpack_from("<H", spl, 12)[0] == len(spl), "SPL length mismatch")

print("Verified native R1 firmware, full ZIP and USB/flash bootloader package")
