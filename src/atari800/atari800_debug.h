//
// atari800_debug.h
//
// SD-card debug log for the BMX atari800 machine.
//
// The Pi runs headless for most users (no UART), so boot progress and the
// atari800 core's own buffered log are tee'd to bmx-atari800.log at the
// root of the first writable SD volume. The user reads it on a PC.
//

#ifndef atari800_debug_h
#define atari800_debug_h

namespace bmx {
namespace atari800 {

// Opens the log file. Safe to call once at emulator startup; falls back to
// printf-only when no volume is writable.
void debug_init(void);

// printf-style line sink: serial (via printf) plus the log file (flushed).
void debug_log(const char *fmt, ...);

// Appends atari800's buffered core log (Log_buffer) to the file and clears
// it. Without this the core's messages are never visible (BUFFERED_LOG).
void debug_dump_core_log(void);

void debug_close(void);

}  // namespace atari800
}  // namespace bmx

#endif
