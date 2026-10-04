import os
import struct
import sys


def make_image(path: str) -> None:
    sectors = 131072
    reserved = 32
    fat_sectors = 1008
    data_start = reserved + 2 * fat_sectors
    clusters = sectors - data_start
    image_size = sectors * 512
    with open(path, "wb") as image:
        image.truncate(image_size)
        boot = bytearray(512)
        boot[0:3] = b"\xeb\x58\x90"
        boot[3:11] = b"RAWLINE "
        struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, 1, reserved, 2, 0, 0, 0xF8, 0, 63, 255, 0, sectors)
        struct.pack_into("<IHHIHH", boot, 36, fat_sectors, 0, 0, 2, 1, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0x5241574C)
        boot[71:82] = b"RAWLINE    "
        boot[82:90] = b"FAT32   "
        boot[510:512] = b"\x55\xaa"
        image.seek(0)
        image.write(boot)

        fsinfo = bytearray(512)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488, clusters - 1)
        struct.pack_into("<I", fsinfo, 492, 3)
        fsinfo[510:512] = b"\x55\xaa"
        image.seek(512)
        image.write(fsinfo)
        image.seek(6 * 512)
        image.write(boot)
        image.seek(7 * 512)
        image.write(fsinfo)

        first_fat = bytearray(512)
        struct.pack_into("<III", first_fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
        image.seek(reserved * 512)
        image.write(first_fat)
        image.seek((reserved + fat_sectors) * 512)
        image.write(first_fat)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: mkfat32.py image")
    target = sys.argv[1]
    if os.path.exists(target):
        raise SystemExit(f"refusing to replace existing data image: {target}")
    make_image(target)
    print(f"created FAT32 image: {target}")
