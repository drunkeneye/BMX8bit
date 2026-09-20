#!/usr/bin/env python3
"""Create a two-partition BMX SD card.

Layout:
  1. BMX BOOT, at least 512 MiB FAT32, boot/system files
  2. BMX USER, remaining space FAT32, user-controlled files
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


DEFAULT_BOOT_SIZE_MIB = 512
MIN_BOOT_SIZE_MIB = 512
ONLINE_UPDATE_BOOT_SIZE_MIB = 512
BOOT_LABEL = "BMX BOOT"
USER_LABEL = "BMX USER"
USER_TOP_LEVEL_DIRS = {"disks", "tapes", "carts", "snapshots", "phonebooks"}
MACHINE_DIRS = ("C64", "SCPU64", "C128", "VIC20", "PLUS4", "PET")


def run(cmd, *, check=True):
    print("+", " ".join(str(part) for part in cmd))
    return subprocess.run(cmd, check=check, text=True)


def capture(cmd):
    return subprocess.check_output(cmd, text=True).strip()


def require_tool(name):
    if shutil.which(name) is None:
        raise SystemExit(f"required tool not found: {name}")


def partition_path(device, number):
    name = os.path.basename(device)
    if name.startswith(("mmcblk", "nvme")):
        return f"{device}p{number}"
    return f"{device}{number}"


def block_type(device):
    try:
        return capture(["lsblk", "-dn", "-o", "TYPE", device])
    except subprocess.CalledProcessError:
        return ""


def logical_sector_size(device):
    try:
        return int(capture(["blockdev", "--getss", device]))
    except (subprocess.CalledProcessError, ValueError):
        return 512


def mounted_at(path):
    result = subprocess.run(
        ["findmnt", "-n", "-S", path, "-o", "TARGET"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
    )
    return [line for line in result.stdout.splitlines() if line]


def ensure_unmounted(device, allow_unmount):
    paths = [device, partition_path(device, 1), partition_path(device, 2)]
    mounts = []
    for path in paths:
        mounts.extend((path, mount) for mount in mounted_at(path))

    if not mounts:
        return

    if not allow_unmount:
        print("mounted filesystems found:", file=sys.stderr)
        for path, mount in mounts:
            print(f"  {path} -> {mount}", file=sys.stderr)
        raise SystemExit("unmount first or pass --unmount")

    for path, mount in reversed(mounts):
        print(f"unmounting {path} from {mount}")
        # Unmount by the unambiguous block-device source.  In raw output,
        # findmnt escapes spaces in targets as ``\x20``; passing that display
        # form to umount as a path fails for labels such as "BMX BOOT".
        run(["umount", "--", path])


def tree_size(path):
    total = 0
    for root, dirs, files in os.walk(path):
        del dirs
        for name in files:
            try:
                total += (Path(root) / name).stat().st_size
            except OSError:
                pass
    return total


def copy_boot_tree(stage_dir, boot_mount):
    stage_dir = Path(stage_dir)
    boot_mount = Path(boot_mount)

    for entry in stage_dir.iterdir():
        if entry.name.lower() in USER_TOP_LEVEL_DIRS:
            continue

        dest = boot_mount / entry.name
        if entry.is_dir():
            shutil.copytree(entry, dest, symlinks=True, dirs_exist_ok=True)
        else:
            shutil.copy2(entry, dest, follow_symlinks=False)


def make_user_dirs(user_mount):
    user_mount = Path(user_mount)
    for top in ("disks", "tapes", "carts", "snapshots"):
        for machine in MACHINE_DIRS:
            (user_mount / top / machine).mkdir(parents=True, exist_ok=True)
    (user_mount / "phonebooks").mkdir(parents=True, exist_ok=True)


def wait_for_partitions(device):
    part1 = partition_path(device, 1)
    part2 = partition_path(device, 2)
    for _ in range(40):
        if Path(part1).exists() and Path(part2).exists():
            return part1, part2
        time.sleep(0.25)
    raise SystemExit(f"partition devices did not appear: {part1}, {part2}")


def write_partitions(device, boot_size_mib):
    sector_size = logical_sector_size(device)
    start_sector = 1024 * 1024 // sector_size
    boot_sectors = boot_size_mib * 1024 * 1024 // sector_size
    user_start_sector = start_sector + boot_sectors
    spec = f"""label: dos

start={start_sector}, size={boot_sectors}, type=c
start={user_start_sector}, type=c
"""
    proc = subprocess.run(["sfdisk", device], input=spec, text=True)
    if proc.returncode != 0:
        raise SystemExit("sfdisk failed")
    run(["sfdisk", "--activate", device, "1"])
    subprocess.run(["partprobe", device], check=False)
    subprocess.run(["udevadm", "settle"], check=False)


def format_partitions(part1, part2):
    mkfs = shutil.which("mkfs.vfat") or shutil.which("mkfs.fat")
    if mkfs is None:
        raise SystemExit("required tool not found: mkfs.vfat or mkfs.fat")
    run([mkfs, "-F", "32", "-n", BOOT_LABEL, part1])
    run([mkfs, "-F", "32", "-n", USER_LABEL, part2])


def boot_size_mib(value):
    """Parse and validate the requested boot partition size."""
    try:
        size = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("boot size must be an integer in MiB") from error
    if size < MIN_BOOT_SIZE_MIB:
        raise argparse.ArgumentTypeError(
            f"boot size must be at least {MIN_BOOT_SIZE_MIB} MiB"
        )
    return size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="target block device, e.g. /dev/sdX")
    parser.add_argument("--stage-dir", required=True, help="staged BMX boot files")
    parser.add_argument(
        "--boot-size-mib",
        type=boot_size_mib,
        default=DEFAULT_BOOT_SIZE_MIB,
        metavar="MIB",
        help=(
            f"boot partition size in MiB (default: {DEFAULT_BOOT_SIZE_MIB}; "
            f"minimum: {MIN_BOOT_SIZE_MIB})"
        ),
    )
    parser.add_argument("--yes", action="store_true", help="actually repartition and format")
    parser.add_argument("--unmount", action="store_true", help="unmount target partitions first")
    args = parser.parse_args()

    for tool in ("lsblk", "findmnt", "blockdev", "sfdisk", "partprobe", "mount", "umount"):
        require_tool(tool)

    device = os.path.realpath(args.device)
    stage_dir = Path(args.stage_dir).resolve()

    if os.geteuid() != 0 and args.yes:
        raise SystemExit("destructive mode requires root")
    if not Path(device).exists():
        raise SystemExit(f"device does not exist: {device}")
    if block_type(device) != "disk":
        raise SystemExit(f"refusing non-disk block device: {device}")
    if not stage_dir.is_dir():
        raise SystemExit(f"stage directory does not exist: {stage_dir}")

    stage_bytes = tree_size(stage_dir)
    boot_bytes = args.boot_size_mib * 1024 * 1024
    if stage_bytes > boot_bytes * 8 // 10:
        raise SystemExit(
            f"stage directory is too large for {args.boot_size_mib} MiB boot partition"
        )

    print("Target layout:")
    print(
        f"  {partition_path(device, 1)}  {BOOT_LABEL}  FAT32  "
        f"{args.boot_size_mib} MiB"
    )
    print(f"  {partition_path(device, 2)}  {USER_LABEL}  FAT32  remaining space")
    print(f"  stage size: {stage_bytes / (1024 * 1024):.1f} MiB")
    print()

    if not args.yes:
        print("Dry run only. Re-run with --yes to repartition and format.")
        return 0

    ensure_unmounted(device, args.unmount)
    write_partitions(device, args.boot_size_mib)
    part1, part2 = wait_for_partitions(device)
    format_partitions(part1, part2)

    with tempfile.TemporaryDirectory(prefix="bmx-sd-") as tmp:
        boot_mount = Path(tmp) / "boot"
        user_mount = Path(tmp) / "user"
        boot_mount.mkdir()
        user_mount.mkdir()

        try:
            run(["mount", part1, str(boot_mount)])
            run(["mount", part2, str(user_mount)])
            copy_boot_tree(stage_dir, boot_mount)
            make_user_dirs(user_mount)
            run(["sync"])
        finally:
            subprocess.run(["umount", str(user_mount)], check=False)
            subprocess.run(["umount", str(boot_mount)], check=False)

    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
