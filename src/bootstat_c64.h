#ifndef BOOTSTAT_C64
#define BOOTSTAT_C64

int dflt_bootStatNum = 6;

int dflt_bootStatWhat[] = {
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_STAT,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
    BOOTSTAT_WHAT_FAIL,
};

const char *dflt_bootStatFile[] = {
    "kernal-901227-03.bin",
    "basic-901226-01.bin",
    "chargen-901225-01.bin",
    "fliplist-C64.vfl",
    "dos2000",
    "dos4000",
};

int dflt_bootStatSize[] = {
    8192,
    8192,
    4096,
    0,
    0,
    0
};

#endif
