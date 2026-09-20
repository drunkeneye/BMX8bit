//
// atari800_emulator_core.cpp
//
// Atari 800 emulator core integration for BMX bare-metal
//

#include "atari800_emulator_core.h"

extern "C" {
#include "third_party/atari800/src/libatari800/libatari800.h"
}

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/atari800/src/antic.h"
#include "third_party/atari800/src/atari.h"
#include "third_party/atari800/src/cpu.h"
#include "third_party/atari800/src/screen.h"

// Wall-clock seconds for the core (Util_time() -> Screen speed %, fps).
// CTimer ticks are microseconds; libc clock() on Circle newlib does not
// advance in seconds, which froze the speed readout at 0%.
extern "C" double PLATFORM_Time(void) {
  return (double)CTimer::GetClockTicks() * 1e-6;
}
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in atari800_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
#include "version.h"
#include "third_party/common/semaphore.h"
extern void circle_kernel_core_init_complete(int core);
}

#include "atari800_video.h"
#include "atari800_sound.h"
#include "atari800_input.h"
#include "atari800_config.h"
#include "atari800_debug.h"

#ifndef SAMPLE_RATE
#define SAMPLE_RATE 44100
#endif

Atari800EmulatorCore::Atari800EmulatorCore(CMemorySystem *pMemorySystem,
                                           int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
}

Atari800EmulatorCore::~Atari800EmulatorCore(void) {}

void Atari800EmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void Atari800EmulatorCore::RunMainAtari800(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::atari800::debug_init();
  bmx::atari800::debug_log("Starting Atari800 main loop, tv_mode=%s",
                           timing_option_);

  // Publish our machine class (VICE does this in RunMainProgram, which has
  // no atari800 equivalent) and build the BMX menu tree. Each VICE machine
  // calls ui_init_menu() from its Xui_init(); nothing did it for ATARI800,
  // leaving current_menu=-1 and an empty, invisible menu.
  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::atari800::debug_log("Atari800: machine class published, menu built");

  bmx::atari800::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::atari800::config_init(config)) {
    bmx::atari800::debug_log("Atari800 config init FAILED");
    bmx::atari800::debug_dump_core_log();
    return;
  }
  bmx::atari800::debug_log("Atari800 config init OK");
  bmx::atari800::debug_dump_core_log();

  RunFrameLoop();
}

void Atari800EmulatorCore::RunFrameLoop(void) {
  input_template_t input;
  libatari800_clear_input_array(&input);

  bool ntsc = bmx::atari800::config_is_ntsc();
  float frame_fps = libatari800_get_fps();
  unsigned long frame_ticks = (unsigned long)(1000000.0f / frame_fps);

  printf("Atari800 frame loop starting (%s, %.4f fps)\n",
         ntsc ? "NTSC" : "PAL", frame_fps);

  bmx::atari800::video_init();
  bmx::atari800::sound_init();
  bmx::atari800::debug_dump_core_log();

  // CTimer::GetClockTicks() returns microseconds since boot; frame_ticks is
  // also microseconds (1e6/fps). Keep both in us (no /1000 division).
  unsigned long next_frame_us = CTimer::GetClockTicks();
  unsigned long frame_count = 0;
  int consecutive_errors = 0;
  // Per-stage profiling (microseconds, windowed). CTimer ticks are us.
  unsigned long prof_emu = 0, prof_rnd = 0, prof_snd = 0;
  unsigned long prof_emu_max = 0, prof_rnd_max = 0, prof_snd_max = 0;
  unsigned long prof_win_start = CTimer::GetClockTicks();

  while (true) {
    unsigned long now_us = CTimer::GetClockTicks();
    if (!bmx::atari800::warp_enabled()) {
      if ((long)(now_us - next_frame_us) < 0) {
        CTimer::SimpleMsDelay(0);
        continue;
      }
      // Step by whole frames; if we fell more than 5 frames behind (menu,
      // disk IO), resync instead of burst-catching-up.
      next_frame_us += frame_ticks;
      if ((long)(now_us - next_frame_us) > (long)(frame_ticks * 5)) {
        next_frame_us = now_us + frame_ticks;
      }
    } else {
      next_frame_us = now_us + frame_ticks;
    }

    bmx::atari800::poll_input(&input);

    // Same slot as VICE's videoarch pump: consumes ui_toggle_pending (F12)
    // and hotkey quick functions. Without this the menu never opens.
    ui_handle_toggle_or_quick_func();

    bool trace = frame_count < 3;
    if (trace) {
      bmx::atari800::debug_log("Atari800 frame %lu: next_frame enter",
                               frame_count);
    }
    unsigned long t0 = CTimer::GetClockTicks();
    int result = libatari800_next_frame(&input);
    unsigned long t1 = CTimer::GetClockTicks();
    if (trace) {
      bmx::atari800::debug_log("Atari800 frame %lu: next_frame exit result=%d",
                               frame_count, result);
    }
    if (!result) {
      // DLIST/SELFTEST/MEMOPAD/UNIDENTIFIED_CART are not fatal: the OS
      // needs several frames after a cold start before it programs a
      // display list, so DLIST_ERROR fires on early-boot frames of a
      // perfectly healthy boot (upstream's own test ignores these codes
      // and keeps emulating). Only genuine CPU faults reboot.
      int code = libatari800_error_code;
      if (code == LIBATARI800_DLIST_ERROR || code == LIBATARI800_SELF_TEST ||
          code == LIBATARI800_MEMO_PAD ||
          code == LIBATARI800_UNIDENTIFIED_CART_TYPE) {
        if ((frame_count % 300) == 0) {
          bmx::atari800::debug_log("Atari800 frame %lu note: %s (continuing)",
                                   frame_count, libatari800_error_message());
        }
        consecutive_errors = 0;
      } else {
        // NOTE: emulator_state_t is ~210KB: it must come from the heap,
        // never the stack (core stacks are small; a stack copy wedges the
        // machine with no further log output).
        bmx::atari800::debug_log("Atari800 frame %lu error: %s", frame_count,
                                 libatari800_error_message());
        emulator_state_t *err_state =
            (emulator_state_t *)malloc(sizeof(emulator_state_t));
        if (err_state != NULL) {
          // Snapshot CPU/ANTIC state so a dead machine can be diagnosed
          // from the log alone.
          libatari800_get_current_state(err_state);
          const cpu_state_t *cpu =
              (const cpu_state_t *)&err_state->state[err_state->tags.cpu];
          const pc_state_t *pc =
              (const pc_state_t *)&err_state->state[err_state->tags.pc];
          bmx::atari800::debug_log(
              "Atari800 state: PC=%04x A=%02x X=%02x Y=%02x P=%02x S=%02x "
              "dlist=%04x frames=%d",
              pc->PC, cpu->A, cpu->X, cpu->Y, cpu->P, cpu->S,
              (unsigned)ANTIC_dlist, libatari800_get_frame_number());
          free(err_state);
        }
        bmx::atari800::debug_dump_core_log();
        // A faulted frame (bad disk, menu-driven reinit race) must not
        // wedge the console: cold reboot a few times, then give up with
        // the log intact instead of spinning silently.
        if (++consecutive_errors > 5) {
          bmx::atari800::debug_log(
              "Atari800: %d consecutive frame errors, halting",
              consecutive_errors);
          break;
        }
        bmx::atari800::debug_log("Atari800: cold reboot after fault (%d/5)",
                                 consecutive_errors);
        Atari800_Coldstart();
        continue;
      }
    } else {
      consecutive_errors = 0;
    }

    if (trace) {
      bmx::atari800::debug_log("Atari800 frame %lu: render enter", frame_count);
    }
    bmx::atari800::video_render();
    unsigned long t2 = CTimer::GetClockTicks();
    if (trace) {
      bmx::atari800::debug_log("Atari800 frame %lu: render exit, sound enter",
                               frame_count);
    }
    // In warp mode the audio queue would throttle us to realtime (its
    // device drains at wall-clock speed), so skip sound like VICE does
    // when warping without warp-audio.
    if (!bmx::atari800::warp_enabled()) {
      bmx::atari800::sound_play();
    }
    unsigned long t3 = CTimer::GetClockTicks();
    if (trace) {
      bmx::atari800::debug_log("Atari800 frame %lu: sound exit", frame_count);
    }
    frame_count++;
    prof_emu += t1 - t0;
    prof_rnd += t2 - t1;
    prof_snd += t3 - t2;
    if (t1 - t0 > prof_emu_max) prof_emu_max = t1 - t0;
    if (t2 - t1 > prof_rnd_max) prof_rnd_max = t2 - t1;
    if (t3 - t2 > prof_snd_max) prof_snd_max = t3 - t2;

    // VICE creates the audio device via circle_boot_complete() on its
    // first frame (videoarch boot warp); nothing does it on the atari800
    // path, leaving mViceSound NULL (silence, output NONE). Mirror it.
    if (frame_count == 1) {
      circle_boot_complete();
      bmx::atari800::debug_log("Atari800: boot complete signalled");
    }

    // Heartbeat with a checksum of the emulated screen so a black display
    // can be told apart from a stalled emulator. The KMS present sequence
    // shows whether presents complete (advancing) or are silently skipped.
    // Audio source stats ride along so no separate sound log is needed.
    if ((frame_count % 300) == 0) {
      extern ULONG *Screen_atari;
      unsigned long sum = 0;
      const uint8_t *scr = (const uint8_t *)Screen_atari;
      if (scr != NULL) {
        for (int i = 0; i < Screen_WIDTH * Screen_HEIGHT; i++) {
          sum += scr[i];
        }
      }
      struct circle_present_timing pt;
      int pt_ok = circle_get_last_present_timing(&pt);
      unsigned long inv = 0, skd = 0, ske = 0, cal = 0, byt = 0;
      unsigned int peak = 0;
      int llen = 0, lch = 0, lsz = 0, space = 0, en = 0;
      long written = 0;
      bmx::atari800::sound_debug_snapshot(
          &inv, &skd, &ske, &cal, &byt, &peak, &llen, &lch, &lsz, &written,
          &space, &en);
      unsigned long npres = 0, ensure_us = 0, present_us = 0;
      bmx::atari800::video_present_stats(&npres, &ensure_us, &present_us);
      bmx::atari800::debug_log(
          "Atari800 heartbeat frame=%lu screen=%p sum=%lu fps=%.3f "
          "present=%d seq=%u snd inv=%lu dis=%lu emp=%lu wr=%lu peak=%u "
          "last=%dx%dx%d written=%ld space=%d en=%d npres=%lu ensure=%lu "
          "pus=%lu",
          frame_count, (const void *)Screen_atari, sum,
          (double)libatari800_get_fps(), pt_ok, pt_ok ? pt.sequence : 0u,
          inv, skd, ske, cal, peak, llen, lch, lsz, written, space, en,
          npres, ensure_us, present_us);
      unsigned long wnow = CTimer::GetClockTicks();
      unsigned long wus = wnow - prof_win_start;
      unsigned long wframes = 300;
      // Real frame rate over the window + per-stage avg/max cost (us).
      // Budget at 50Hz PAL is 20000us/frame total.
      bmx::atari800::debug_log(
          "Atari800 perf win=%luus frames=%lu realfps=%lu "
          "emu avg=%lu max=%lu rnd avg=%lu max=%lu snd avg=%lu max=%lu",
          wus, wframes, wus ? (wframes * 1000000UL) / wus : 0,
          prof_emu / wframes, prof_emu_max, prof_rnd / wframes, prof_rnd_max,
          prof_snd / wframes, prof_snd_max);
      prof_emu = prof_rnd = prof_snd = 0;
      prof_emu_max = prof_rnd_max = prof_snd_max = 0;
      prof_win_start = wnow;
      bmx::atari800::debug_dump_core_log();
    }
  }
}

void Atari800EmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=atari800\r\n");
    RunMainAtari800(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    circle_kernel_core_init_complete(nCore);
#endif
    break;
  }

#ifdef BMC64_USE_EMU_MULTICORE
  printf("Core %d idle\n", nCore);
#if AARCH == 64
  asm("dsb sy\n\t"
      "1: wfi\n\t"
      "b 1b\n\t");
#else
  asm("dsb\n\t"
      "1: wfi\n\t"
      "b 1b\n\t");
#endif
#endif
}

bool Atari800EmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void Atari800EmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainAtari800(false);
#endif
}
