#pragma once
#include <stdint.h>
#include "v10_seg00.h"
#include "v10_seg01.h"
#include "v10_seg02.h"
#include "v10_seg03.h"
#include "v10_seg04.h"
#include "v10_seg05.h"
#include "v10_seg06.h"
#include "v10_seg07.h"
#include "v10_seg08.h"
#include "v10_seg09.h"
#include "v10_seg10.h"
#include "v10_seg11.h"
#include "v10_seg12.h"
#include "v10_seg13.h"
#include "v10_seg14.h"
#include "v10_seg15.h"

static constexpr uint16_t MF_ROM_START=0xC000;
static constexpr uint16_t MF_ROM_END=0xE000;
static constexpr uint16_t MF_COLD=0xC000;
struct MfRomSegment { uint16_t address; uint16_t length; const char *hex; };
static const MfRomSegment MF_ROM_SEGMENTS[] = {
  {0xC000, 512, MF_V10_SEG00_HEX},
  {0xC200, 512, MF_V10_SEG01_HEX},
  {0xC400, 512, MF_V10_SEG02_HEX},
  {0xC600, 512, MF_V10_SEG03_HEX},
  {0xC800, 512, MF_V10_SEG04_HEX},
  {0xCA00, 512, MF_V10_SEG05_HEX},
  {0xCC00, 512, MF_V10_SEG06_HEX},
  {0xCE00, 512, MF_V10_SEG07_HEX},
  {0xD000, 512, MF_V10_SEG08_HEX},
  {0xD200, 512, MF_V10_SEG09_HEX},
  {0xD400, 512, MF_V10_SEG10_HEX},
  {0xD600, 512, MF_V10_SEG11_HEX},
  {0xD800, 512, MF_V10_SEG12_HEX},
  {0xDA00, 512, MF_V10_SEG13_HEX},
  {0xDC00, 512, MF_V10_SEG14_HEX},
  {0xDE00, 512, MF_V10_SEG15_HEX},
};
static constexpr uint16_t MF_ROM_SEGMENT_COUNT = sizeof(MF_ROM_SEGMENTS)/sizeof(MF_ROM_SEGMENTS[0]);
