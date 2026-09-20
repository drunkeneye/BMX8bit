//
// atari800_debug.cpp
//
// See atari800_debug.h.
//

#include "atari800_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" {
#include "third_party/atari800/src/log.h"
extern char Log_buffer[];
}

namespace bmx {
namespace atari800 {

namespace {

FILE *s_log = NULL;

// fflush() only reaches the FatFS cache; without f_sync() the directory
// entry keeps size 0 and a power cut loses everything. Sync per line: the
// log volume is tiny and every line must survive a hard power-off.
void sync_log(void) {
  if (s_log == NULL) {
    return;
  }
  fflush(s_log);
  fsync(fileno(s_log));
}

}  // namespace

void debug_init(void) {
  static const char *candidates[] = {
      "SYS:/bmx-atari800.log",
      "SD:/bmx-atari800.log",
      "USER:/bmx-atari800.log",
      "/bmx-atari800.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("Atari800: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMX8bit atari800 debug log\n");
      fprintf(s_log, "BUILD TAG: " __DATE__ " " __TIME__ "\n");
      fflush(s_log);
      fsync(fileno(s_log));
      return;
    }
  }
  printf("Atari800: no writable volume for debug log, printf-only\n");
}

void debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  printf("%s\n", line);
  if (s_log != NULL) {
    fprintf(s_log, "%s\n", line);
    sync_log();
  }
}

void debug_dump_core_log(void) {
#ifdef BUFFERED_LOG
  if (::Log_buffer[0] != '\0') {
    printf("%s", ::Log_buffer);
    if (s_log != NULL) {
      fputs(::Log_buffer, s_log);
      sync_log();
    }
    ::Log_buffer[0] = '\0';
  }
#else
  Log_flushlog();
#endif
}

void debug_close(void) {
  if (s_log != NULL) {
    fclose(s_log);
    s_log = NULL;
  }
}

}  // namespace atari800
}  // namespace bmx
