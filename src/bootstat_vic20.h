#ifndef BOOTSTAT_VIC20
#define BOOTSTAT_VIC20

int dflt_bootStatNum = 18;

int dflt_bootStatWhat[] = {
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
};

const char *dflt_bootStatFile[] = {
    "basic-901486-01.bin",
    "chargen-901460-03.bin",
    "kernal.901486-06.bin",
    "kernal.901486-07.bin",
    "fliplist-VIC20.vfl",
    "mps803",
    "mps803.vpl",
    "nl10-cbm",
    "1520.vpl",
    "dos1540",
    "dos1570",
    "dos2000",
    "dos4000",
    "dos2031",
    "dos2040",
    "dos3040",
    "dos4040",
    "dos1001",
};

int dflt_bootStatSize[] = {
    8192,
    4096,
    8192,
    8192,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

#endif
