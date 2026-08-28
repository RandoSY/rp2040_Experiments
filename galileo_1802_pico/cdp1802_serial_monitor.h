#pragma once

#include <Arduino.h>
#include "cdp1802.h"

// Machine-readable, line-oriented debug protocol for hardware-in-the-loop
// validation of the RP2040 CDP1802 core. Commands such as @STATE and @STEP
// are routed here by the sketch; a bare Forth @ remains part of the normal
// Forth console.
//
// The caller must pause real-time flight execution before invoking this
// handler so state snapshots and single steps are deterministic.
bool handleCdp1802MonitorCommand(Stream &io, CDP1802 &cpu, CDP1802Bus &bus, char *line);
