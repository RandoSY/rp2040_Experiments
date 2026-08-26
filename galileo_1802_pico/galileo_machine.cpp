#include "galileo_machine.h"
#include "crc32.h"
#include <string.h>

bool GalileoMachine::loadImages(const uint8_t *romImage, size_t romLen, const uint8_t *ramImage, size_t ramLen,
                                uint32_t *romCrcOut, uint32_t *ramCrcOut) {
    if (!romImage || !ramImage || romLen != ROM_SIZE || ramLen != RAM_INIT_SIZE) return false;
    uint32_t rc = crc32_ieee(romImage, romLen);
    uint32_t mc = crc32_ieee(ramImage, ramLen);
    if (romCrcOut) *romCrcOut = rc;
    if (ramCrcOut) *ramCrcOut = mc;
    if (rc != EXPECTED_ROM_CRC || mc != EXPECTED_RAM_CRC) return false;
    memcpy(rom, romImage, ROM_SIZE);
    memset(ram, 0, RAM_SIZE);
    memcpy(ram, ramImage, RAM_INIT_SIZE);
    imagesLoaded = true;
    resetHardware();
    return true;
}

void GalileoMachine::resetHardware() {
    memset(scratch, 0, sizeof(scratch));
    memset(hwShadow, 0, sizeof(hwShadow));
    memset(protect, 0, sizeof(protect));
    memset(switches, 0, sizeof(switches));
    selectedChannel = 0;
    adc = mulX = mulY = mulResult = counter = 0;
    interruptCount = 0;
    realFieldValid = false;
}

void GalileoMachine::setRealFieldMilliUt(int32_t x, int32_t y, int32_t z, bool valid, uint32_t sampleCount) {
    realFieldMilliUt[0] = x;
    realFieldMilliUt[1] = y;
    realFieldMilliUt[2] = z;
    realFieldValid = valid;
    realFieldSampleCount = sampleCount;
}

uint8_t GalileoMachine::status7002() const {
    return (switches[0]?0x01:0) | (switches[1]?0x02:0) | (switches[2]?0x04:0) |
           (switches[3]?0x08:0) | (switches[4]?0x10:0) | (switches[5]?0x20:0);
}
uint8_t GalileoMachine::status7003() const {
    return (switches[6]?0x01:0) | (switches[8]?0x02:0) | (switches[9]?0x04:0) |
           (switches[10]?0x08:0) | (switches[11]?0x10:0) | (switches[7]?0x20:0);
}

uint16_t GalileoMachine::adcFor(uint8_t ch) const {
    int raw = 0;

    if (usingRealMag() && realFieldValid && (ch <= 2 || (ch >= 4 && ch <= 6))) {
        const unsigned axis = (ch <= 2) ? ch : (ch - 4);
        raw = (int)(((int64_t)realFieldMilliUt[axis] * realCountsPerUt) / 1000LL);
    } else if (!usingRealMag() && ch <= 2) raw = fields[ch];
    else if (!usingRealMag() && ch >= 4 && ch <= 6) raw = fields[ch-1];
    else if (ch == 8) raw = 1200;
    else if (ch == 9) raw = 1000;
    else if (ch == 10) raw = -1200;
    else if (ch == 11) raw = 1500;
    else if (ch == 12) raw = 250;
    else if (ch == 13) raw = 900;
    else if (ch == 14) raw = -1500;

    if (switches[5] && (ch == 0 || ch == 4)) raw = 1024;

    int v = raw + 2048;
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)v;
}

uint8_t GalileoMachine::read(uint16_t a) {
    const bool swapped = bankSwapped();
    if (a < 0x1000) return swapped ? ram[a] : rom[a];
    if (a >= 0x4000 && a < 0x5000) return swapped ? rom[a-0x4000] : ram[a-0x4000];
    if (a >= 0x6000 && a < 0x6400) return scratch[a-0x6000];
    if (a < 0x7000 || a > 0x7fff) return 0;

    if (a == 0x7033) return 0x70;
    if (a == 0x7002) return status7002();
    if (a == 0x7003) return status7003();

    if ((a & 0xff00u) == 0x7000u) {
        uint8_t ch = (uint8_t)((a >> 3) & 0x0f);
        uint8_t sel = (uint8_t)(a & 7);
        selectedChannel = ch;
        adc = adcFor(ch);
        if (sel == 0) return (uint8_t)adc;
        if (sel == 1) return (uint8_t)(adc >> 8);
        if (sel == 4) return 0xff;
    }
    if ((a & 0xff00u) == 0x7200u) {
        if ((a & 0x0f) == 2) return (uint8_t)mulResult;
        if ((a & 0x0f) == 3) return (uint8_t)(mulResult >> 8);
    }
    if ((a & 0xff00u) == 0x7500u) return (uint8_t)counter;
    return hwShadow[a & 0x0fff];
}

void GalileoMachine::writePhysicalRam(uint16_t pa, uint8_t value) {
    if (pa < 0x4000 || pa >= 0x5000) return;
    if (pa < 0x4800) {
        unsigned page = (pa >> 8) & 7;
        if (protect[page]) return;
    }
    ram[pa-0x4000] = value;
}

void GalileoMachine::write(uint16_t a, uint8_t v) {
    const bool swapped = bankSwapped();
    if (a < 0x1000) {
        if (swapped) writePhysicalRam((uint16_t)(0x4000 + a), v);
        return;
    }
    if (a >= 0x4000 && a < 0x5000) {
        if (!swapped) writePhysicalRam(a, v);
        return;
    }
    if (a >= 0x6000 && a < 0x6400) { scratch[a-0x6000] = v; return; }
    if (a < 0x7000 || a > 0x7fff) return;

    hwShadow[a & 0x0fff] = v;
    if ((a & 0xff00u) == 0x7000u) {
        uint8_t ch = (uint8_t)((a >> 3) & 0x0f);
        uint8_t sel = (uint8_t)(a & 7);
        selectedChannel = ch;
        if (sel == 0 || sel == 1) adc = adcFor(ch);
        return;
    }
    if ((a & 0xff00u) == 0x7200u) {
        switch (a & 0x0f) {
            case 0: mulX = v; break;
            case 1: mulY = v; mulResult = (uint16_t)((uint16_t)mulX * (uint16_t)mulY); break;
            default: break;
        }
        return;
    }
    if (a >= 0x74f0 && a <= 0x74fb) {
        bool on = controlOn(v), off = controlOff(v);
        if (!on && !off) return;
        unsigned idx = a - 0x74f0;
        if (a >= 0x74f8 && a <= 0x74fb && on) {
            if (!switches[6]) return;
            for (unsigned i=8;i<=11;i++) switches[i] = false;
            switches[idx] = true;
            return;
        }
        switches[idx] = on;
        if (a == 0x74f6 && off) for (unsigned i=8;i<=11;i++) switches[i] = false;
        return;
    }
    if ((a & 0xff00u) == 0x7700u && (a & 0x0f) == 0) {
        unsigned page = (a >> 4) & 0x0f;
        if (page < 8) {
            if (controlOn(v)) protect[page] = true;
            else if (controlOff(v)) protect[page] = false;
        }
    }
}

bool GalileoMachine::injectInterrupt(CDP1802 &cpu, bool rti) {
    cpu.setEF(1, rti);
    counter = (uint16_t)(counter + 1);
    if (cpu.interrupt()) { interruptCount++; return true; }
    return false;
}
