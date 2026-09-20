#ifndef BMX_UPDATE_UPDATE_HARDWARE_TEST_MODE_H
#define BMX_UPDATE_UPDATE_HARDWARE_TEST_MODE_H

// A debug test-channel build may expose the real synchronous updater without
// making any statement about production readiness.
#if defined(BMX_UPDATE_HARDWARE_TEST_MODE) && \
    BMX_UPDATE_HARDWARE_TEST_MODE != 1
#error "BMX_UPDATE_HARDWARE_TEST_MODE must be exactly 1 when defined"
#endif

#if defined(BMX_UPDATE_HARDWARE_TEST_MODE) && \
    (!defined(RASPI_COMPILE) || !defined(BMC64_DEBUG_PROFILE) || \
     !defined(BMX_UPDATE_TEST_CHANNEL))
#error "BMX update hardware-test mode requires a Raspberry Pi debug test-channel build"
#endif

// Canonical production releases opt into the reduced updater with one clear
// release decision. The simple updater does not claim tryboot, rollback,
// watchdog or power-loss recovery capabilities.
#if defined(BMX_UPDATE_SIMPLE_PRODUCTION) && \
    BMX_UPDATE_SIMPLE_PRODUCTION != 1
#error "BMX_UPDATE_SIMPLE_PRODUCTION must be exactly 1 when defined"
#endif

#if defined(BMX_UPDATE_SIMPLE_PRODUCTION) && \
    (!defined(RASPI_COMPILE) || defined(BMC64_DEBUG_PROFILE) || \
     defined(BMX_UPDATE_TEST_CHANNEL))
#error "BMX simple production update requires a Raspberry Pi production release build"
#endif

// A signed local ticket keeps the owner-only production-draft route hidden
// from normal users. It is independent of stable production discovery.
#if defined(BMX_UPDATE_OWNER_DRAFT_TEST) && \
    BMX_UPDATE_OWNER_DRAFT_TEST != 1
#error "BMX_UPDATE_OWNER_DRAFT_TEST must be exactly 1 when defined"
#endif

#if defined(BMX_UPDATE_OWNER_DRAFT_TEST) && \
    (!defined(RASPI_COMPILE) || defined(BMC64_DEBUG_PROFILE) || \
     defined(BMX_UPDATE_TEST_CHANNEL))
#error "BMX owner draft testing requires a Raspberry Pi production release build"
#endif

#if defined(BMX_UPDATE_HARDWARE_TEST_MODE) || \
    defined(BMX_UPDATE_SIMPLE_PRODUCTION)
#define BMX_UPDATE_ENABLE_TARGET_UI 1
#endif

#if defined(BMX_UPDATE_OWNER_DRAFT_TEST) || \
    defined(BMX_UPDATE_ENABLE_TARGET_UI)
#define BMX_UPDATE_ENABLE_OWNER_DRAFT_UI 1
#endif

#endif  // BMX_UPDATE_UPDATE_HARDWARE_TEST_MODE_H
