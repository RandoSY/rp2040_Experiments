#pragma once
#include "cdp1802.h"
#include <stdint.h>
#include <stddef.h>

class GalileoMachine : public CDP1802Bus {
public:
    static constexpr size_t ROM_SIZE = 0x1000;
    static constexpr size_t RAM_SIZE = 0x1000;
    static constexpr size_t RAM_INIT_SIZE = 0x0700;
    static constexpr size_t SCRATCH_SIZE = 0x0400;
    static constexpr uint32_t EXPECTED_ROM_CRC = 0x779E96F2u;
    static constexpr uint32_t EXPECTED_RAM_CRC = 0xA06242CAu;

    uint8_t rom[ROM_SIZE]{};
    uint8_t ram[RAM_SIZE]{};
    uint8_t scratch[SCRATCH_SIZE]{}; // emulator-private call trampoline at $6000-$63FF
    uint8_t hwShadow[0x1000]{};
    bool protect[8]{};
    bool switches[12]{}; // $74F0..$74FB
    int16_t fields[6] = {120,-80,420,100,-60,390}; // deterministic SIM fixture, ADC-count deltas

    enum class MagBackend : uint8_t { Simulated=0, RealGY271=1 };
    MagBackend magBackend = MagBackend::Simulated;
    int32_t realFieldMilliUt[3] = {0,0,0}; // physical field in 0.001 uT units
    bool realFieldValid = false;
    uint32_t realFieldSampleCount = 0;
    int16_t realCountsPerUt = 10;           // provisional physical->Galileo ADC adapter
    uint8_t selectedChannel = 0;
    uint16_t adc = 0;
    uint8_t mulX = 0, mulY = 0;
    uint16_t mulResult = 0;
    uint16_t counter = 0;
    uint32_t interruptCount = 0;
    bool imagesLoaded = false;

    bool loadImages(const uint8_t *romImage, size_t romLen, const uint8_t *ramImage, size_t ramLen,
                    uint32_t *romCrcOut=nullptr, uint32_t *ramCrcOut=nullptr);
    void resetHardware();

    uint8_t read(uint16_t address) override;
    void write(uint16_t address, uint8_t value) override;
    uint8_t input(uint8_t port) override { (void)port; return 0; }
    void output(uint8_t port, uint8_t value) override { (void)port; (void)value; }

    uint8_t status7002() const;
    uint8_t status7003() const;
    uint16_t adcFor(uint8_t channel) const;
    void setRealFieldMilliUt(int32_t x, int32_t y, int32_t z, bool valid, uint32_t sampleCount=0);
    void useRealMag(bool enable) { magBackend = enable ? MagBackend::RealGY271 : MagBackend::Simulated; }
    bool usingRealMag() const { return magBackend == MagBackend::RealGY271; }
    bool injectInterrupt(CDP1802 &cpu, bool rti=true);

    bool bankSwapped() const { return switches[0]; }
    bool ramProtected(unsigned page) const { return page < 8 ? protect[page] : false; }
    uint8_t *ramPtr(uint16_t logical) { return (logical>=0x4000 && logical<0x5000) ? &ram[logical-0x4000] : nullptr; }
    const uint8_t *ramPtr(uint16_t logical) const { return (logical>=0x4000 && logical<0x5000) ? &ram[logical-0x4000] : nullptr; }

private:
    bool controlOn(uint8_t v) const { return v == 0xAB || v == 0xBB; }
    bool controlOff(uint8_t v) const { return v == 0xAA; }
    void writePhysicalRam(uint16_t physicalAddress, uint8_t value);
};
