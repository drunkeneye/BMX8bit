Version 0.1 (2026-09-20)
------------------
First BMX8bit release: BMX at commit `81efb7e` (public export 2026.08.19)
plus the Atari 8-bit port. Upstream history lives on in
[`CHANGELOG_BMX.md`](CHANGELOG_BMX.md).

* New: Atari 8-bit machine, powered by a pristine vendored
  atari800 7.2.1 core (`libatari800` target; only `src/config.h` is
  BMX-specific). `NEW_CYCLE_EXACT` is enabled like on every desktop
  build, so cycle-counted kernels (e.g. RastaConverter titles) render
  without color smears.
* New: machine models 400/800, 600XL, 800XL/XE with RAM sizes 8K..1088K,
  R-Time 8, slow XEX loading, PAL/NTSC, BASIC on/off.
* New: ROM handling. At boot the kernel scans `/roms` and matches
  OS/BASIC/5200/drive ROMs by CRC32/size (filenames do not matter);
  compiled-in Altirra ROMs boot zero-config when nothing matches.
  `build_piX.sh --download-roms` (on by default) fetches missing
  firmware before staging: VICE ROMs from zimmers.net (byte-verified
  against known-good dumps; SuperCPU binary excluded) into `roms/`,
  Atari ROMs from FW-Altirra `Automatic/` into `roms/a800/`
  (staged 1:1 to `/roms`). Present files are never re-downloaded.
* New: monitor modes (Color, B&W, Green, Amber), PAL delay-line video
  blend, uncompressed BMP screenshots of the displayed frame.
* New: sound menu (dual-POKEY stereo, HiFi, key click, Bienias fix,
  output latency; fixed 44100 Hz / 16-bit sink) and atari800 license
  entry under Licenses.
* New: USB keyboard with raw hardware arrow codes, F1..F5 fixed to
  Help/Start/Select/Option/Reset, user-mappable F6..F11 (None, Help,
  Start, Select, Option, Reset, Menu), persisted in settings.
* New: combined hat+analog USB joystick handling on both ports.
* New: SIO drives D1:..D8: with state/read-only handling, `.ats`
  disk-set save/load, blank ATR creation, gz/DCM support, disk
  rotate, SIO-patch toggle, disk/sector/1200XL LEDs and speed display.
* New: cartridge attach/detach with reboot-on-change toggle and a
  blank-cartridge creator (8K/16K/AtariMax 1M, valid headers).
* New: ZIP browser for all attach flows (disks, carts, tapes):
  nested archives, 32000+ entry packs, tolerant open (tool metadata,
  Windows paths, data descriptors, flag/country quirks), single-pass
  CRC-verified extraction with progress bar and ESC cancel, sorted
  dirs-first listings with type-ahead.
* New: BMX8bit branding (About shows `BMX8bit 0.1`, project
  `https://github.com/drunkeneye/BMX8bit` next to the kept
  `https://github.com/kdre/bmx`), `Autostart...` label, Atari-cleaned
  Sound/Keyboard menus (no SID, no CBM hotkeys, no mapping editor).
* Infra: build-identity stamps rebuilt on every build (About + debug
  log always identify the running kernel), toolchain download retries
  once on truncated archives, `VERSION` file (`0.1`).
* Disabled: System Update and draft-test menus are listed but greyed
  out (no update channel in this fork); same for the Developer
  submenu (unsupported, errors when enabled).

Known issues (0.1)
------------------
* Attaching files from very large nested archives can still hang the
  menu right after extraction; under diagnosis (reboot-persistent
  `zip:` trace lines in the debug log pinpoint the phase).
* These ROMs cannot be auto-downloaded and stay manual (see
  `MISSING-ROMS.txt`): the SuperCPU `scpu64` binary, seven drive/PET
  outliers (`dos1001`, `dos2031`, `dos2040`, `dos3040`, `dos4040`,
  `dos9000`, `edit-4-80-b`), whose upstream copies are split,
  ambiguous, or revision-mismatched.
* The keyboard mapping editor is hidden for the Atari port (returns
  in a later release).
* Atari on Pi 5 is staged but not device-tested; Pi 4 is verified.
