// Firmware identity. CI overrides FIRMWARE_VERSION from the git tag via
// PLATFORMIO_BUILD_FLAGS; a USB build is "dev" and never auto-updates (see DESIGN.md §13).
#pragma once
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
#ifndef BOARD_VARIANT
#error "BOARD_VARIANT must be defined per environment in platformio.ini"
#endif
