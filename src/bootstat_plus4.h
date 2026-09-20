#ifndef BOOTSTAT_PLUS4
#define BOOTSTAT_PLUS4

int dflt_bootStatNum = 18;

int dflt_bootStatWhat[] = {
    BOOTSTAT_WHAT_STAT,
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
};

const char *dflt_bootStatFile[] = {
    "kernal-318004-05.bin",
    "kernal-318005-05.bin",
    "basic-318006-01.bin",
    "3plus1-317053-01.bin",
    "3plus1-317054-01.bin",
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
    16384,
    16384,
    16384,
    16384,
    16384,
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
};

#endif
