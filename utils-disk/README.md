# BMX Utility Disks

The Pi4/Pi5 staging scripts build per-machine utility disk images from this
tree:

- `c64/` -> `utils/c64/utils.d64`
- `c128/` -> `utils/c128/utils.d64`
- `vic20/` -> `utils/vic20/utils.d64`
- `plus4/` -> `utils/plus4/utils.d64`
- `pet/` -> `utils/pet/utils.d64`

Only regular, non-hidden files are copied into the generated disk image.
Generated `utils.d64` files are staging artifacts and are not stored in git.

The machine-specific image is the initial selection under
`Drives > Default disk`. The menu can select another disk image, choose drive
8 through 11, or disable automatic insertion with `None`.
