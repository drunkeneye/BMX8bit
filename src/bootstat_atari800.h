#ifndef BOOTSTAT_ATARI800
#define BOOTSTAT_ATARI800

// The Atari 800 build compiles the Altirra OS/BASIC ROMs in via EMUOS_ALTIRRA,
// so there are no external ROM files to check at boot.

int dflt_bootStatNum = 0;

int dflt_bootStatWhat[] = {
};

const char *dflt_bootStatFile[] = {
};

int dflt_bootStatSize[] = {
};

#endif
