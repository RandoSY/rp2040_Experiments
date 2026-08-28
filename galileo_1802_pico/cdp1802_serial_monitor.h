#pragma once

#include <Arduino.h>
#include "cdp1802.h"

// Machine-readable, line-oriented debug protocol for hardware-in-the-loop
// validation of the RP2040 CDP1802 core. Commands begin with '@' so they do
// not collide with the existing Forth words, '.' monitor commands, or '!'
// image-upload protocol.
bool handleCdp1802MonitorCommand(Stream &io, CDP1802 &cpu, CDP1802Bus &bus, char *line);
