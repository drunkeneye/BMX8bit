#ifndef _bmc64_circle_glue_h
#define _bmc64_circle_glue_h

#include <circle/serial.h>
#include <stdint.h>

class CNetSubSystem;

#define MAX_BOOTSTAT_LINES 32
#define MAX_BOOTSTAT_FLEN 64

#define BOOTSTAT_WHAT_STAT 0
#define BOOTSTAT_WHAT_FAIL 1

void CGlueStdioInit(CSerialDevice *serial);

// Physical FatFs activity performed by the BMX stdio replacement. Logical
// reads served by the bounded in-memory cache do not increment read_calls.
// The counters are populated only in builds with BMC64_IO_STATS enabled.
struct CGlueIOStats {
  uint64_t read_calls;
  uint64_t read_bytes;
  uint64_t read_us;
  uint64_t read_max_us;
  uint64_t seek_calls;
  uint64_t seek_us;
  uint64_t seek_max_us;
  uint64_t slurp_calls;
  uint64_t slurp_bytes;
  uint64_t slurp_us;
  uint64_t slurp_max_us;
  uint64_t write_calls;
  uint64_t write_bytes;
  uint64_t write_us;
  uint64_t write_max_us;
  uint64_t sync_calls;
  uint64_t direct_open_calls;
  uint64_t cached_open_calls;
  uint64_t cache_spills;
  uint64_t io_errors;
  uint32_t open_files;
  uint32_t peak_open_files;
};

void CGlueStdioGetIOStats(CGlueIOStats *stats);
void CGlueStdioResetIOStats(void);

void CGlueStdioInitBootStat(int num,
                            int *bootStatWhat,
                            const char **bootStatFile,
                            int *bootStatSize);

int CGlueStdioShutdown(void);

void CGlueNetworkInit(CNetSubSystem &network);

#endif
