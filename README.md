# BMX8bit 0.1

BMX8bit is bare-metal emulation for the Raspberry Pi 4/5 family:
the Commodore machines of BMX plus an Atari 8-bit port.

This is a fork of [BMX](https://github.com/kdre/bmx) at commit
`81efb7e` (public source export 2026.08.19). All credit for the
foundation, BMX itself, and through it BMC64, VICE, Circle and the
whole bare-metal stack, goes to the respective authors. Your
work is legendary. Every change on
top of that base is mine; upstream history is preserved verbatim in
[`CHANGELOG_BMX.md`](CHANGELOG_BMX.md).

> Built with AI assistance: vibe-coded with Muse Spark 1.3 via
> [opencode.ai](https://opencode.ai). This is not a fully supported
> release, it is unclear whether any reported bugs will ever be
> fixed. It runs the Numen and Control demos, and that is enough
> for me, right now, right here.


## What changed (0.1)

- Atari 8-bit machine powered by vendored atari800 7.2.1
  (see `THIRD_PARTY_SOURCES.md`).
- Automatic firmware ROM download during staging
  (`--download-roms`, on by default): VICE ROMs from zimmers.net,
  Atari ROMs from FW-Altirra. Files already present are kept;
  whatever cannot be fetched is reported in `MISSING-ROMS.txt`.
- Monitor modes, PAL video blend, BMP screenshots.
- Sound menu (stereo dual POKEY, HiFi, click, Bienias fix, latency).
- USB keyboard: fixed F1..F5 console keys, user-mappable F6..F11,
  raw hardware arrow codes; combined hat+analog joysticks.
- SIO drives D1:..D8:, disk sets, blank ATRs, SIO patch, LEDs.
- Cartridges with reboot-on-change and a blank creator.
- ZIP browser with nested archives, progress bar and cancel.
- Full list: [`CHANGELOG.md`](CHANGELOG.md).

## What's missing / known issues

- Attaching from very large nested archives can hang after
  extraction; under diagnosis, see `CHANGELOG.md`.
- The SuperCPU binary plus seven drive/PET ROM variants cannot be
  auto-downloaded and stay manual (see `MISSING-ROMS.txt`).
- The keyboard mapping editor is hidden for the Atari port for now.
- Atari on Pi 5 is staged but not device-tested.
- System updates are disabled (menu entries greyed out): there is no
  update channel in this fork. Install new releases by writing a fresh
  SD image. The Developer submenu is greyed out for the same reason.

## Building

Build, staging, installation and SD-card creation are documented in
[`BUILDING.md`](BUILDING.md). A fresh clone builds with:

```sh
tools/pi4/build_pi4.sh
```

which fetches the ARM toolchain, all missing ROMs, then stages a
bootable tree (default `pi4-test/sdcard`).

It will also download intros and demos from ftp.pigwa.net and place
it into the disks folders, just that you have some cool stuff to start with.


## Credits

- BMX / BMC64 and all third parties listed in
  [`THIRD_PARTY_SOURCES.md`](THIRD_PARTY_SOURCES.md) and the on-device
  Licenses menu — everything we stand on.
- atari800 (GPL-2.0) by the Atari800 team; Altirra OS/BASIC for the
  zero-config fallback.
- Atari firmware pool: [FW-Altirra](https://github.com/ascrnet/FW-Altirra).
- Commodore firmware: [zimmers.net](https://www.zimmers.net/anonftp/pub/cbm/firmware/).


