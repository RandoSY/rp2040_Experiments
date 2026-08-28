#pragma once
#include <stdint.h>
#include "v9_seg0.h"
#include "v9_seg1.h"
#include "v9_seg2.h"
#include "v9_seg3.h"
#include "v9_seg4.h"
#include "v9_seg5.h"
#include "v9_seg6.h"
#include "v9_seg7.h"
#include "v9_seg8.h"
#include "v9_seg9.h"
#include "v9_seg10.h"
#include "v9_seg11.h"
#include "v9_seg12.h"

static constexpr uint16_t MF_ROM_START=0xC000;
static constexpr uint16_t MF_ROM_END=0xE000;
static constexpr uint16_t MF_COLD=0xC000;
static constexpr uint16_t MF_NEXT=0xC101;
static constexpr uint16_t MF_OUTER=0xC800;
struct MfRomSegment { uint16_t address; uint16_t length; const char *hex; };
static const MfRomSegment MF_ROM_SEGMENTS[] = {
  {0xC000, 25, MF_SEG0_HEX},
  {0xC100, 1106, MF_SEG1_HEX},
  {0xC800, 592, MF_SEG2_HEX},
  {0xCC00, 16, MF_SEG3_HEX},
  {0xCC40, 33, MF_SEG4_HEX},
  {0xCCA0, 104, MF_SEG5_HEX},
  {0xCD20, 86, MF_SEG6_HEX},
  {0xCE00, 158, MF_SEG7_HEX},
  {0xCF20, 28, MF_SEG8_HEX},
  {0xD000, 129, MF_SEG9_HEX},
  {0xD400, 700, MF_SEG10_HEX},
  {0xD800, 34, MF_SEG11_HEX},
  {0xD840, 124, MF_SEG12_HEX},
};
static constexpr uint16_t MF_ROM_SEGMENT_COUNT = sizeof(MF_ROM_SEGMENTS)/sizeof(MF_ROM_SEGMENTS[0]);
