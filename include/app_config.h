#pragma once

#define BATTERY_DISPLAY_VERSION "15.7-FIXED-LIGHT"

// Local owner-specific defaults are intentionally excluded from the public
// repository. Copy local_config.example.h to local_config.h for a private
// build that should preserve settings on one known display.
#if __has_include("local_config.h")
#include "local_config.h"
#endif

#ifndef LOCAL_OWNER_DISPLAY_MAC
#define LOCAL_OWNER_DISPLAY_MAC ""
#endif

#ifndef LOCAL_OWNER_EMULATOR_HOST
#define LOCAL_OWNER_EMULATOR_HOST ""
#endif

static constexpr uint16_t CELL_LOW_WARNING_MV = 3000;
static constexpr uint16_t CELL_HIGH_WARNING_MV = 4250;
