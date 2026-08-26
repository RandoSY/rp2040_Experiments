#pragma once
#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#endif

enum class GY271Chip : uint8_t {
    None = 0,
    HMC5883L = 1,
    QMC5883L = 2
};

struct GY271Telemetry {
    GY271Chip chip = GY271Chip::None;
    uint8_t address = 0;
    bool configured = false;
    bool valid = false;
    bool overflow = false;
    int16_t rawX = 0, rawY = 0, rawZ = 0;
    int32_t milliUtX = 0, milliUtY = 0, milliUtZ = 0;
    uint32_t sampleCount = 0;
    uint32_t errorCount = 0;
    uint32_t lastSampleMs = 0;
};

// One writer (RP2040 core 1), one or more readers (core 0).  A simple
// sequence lock avoids taking a mutex from the emulated-1802 timing path.
class GY271Mailbox {
public:
    void publish(const GY271Telemetry &value);
    bool snapshot(GY271Telemetry &out) const;
private:
    volatile uint32_t sequence_ = 0;
    GY271Telemetry value_{};
};

extern GY271Mailbox g_gy271Mailbox;
extern volatile uint32_t g_gy271RescanRequest;
extern volatile uint32_t g_gy271RescanDone;

const char *gy271ChipName(GY271Chip chip);

#ifdef ARDUINO
class GY271Sensor {
public:
    bool begin(TwoWire &wire, int sdaPin=4, int sclPin=5);
    bool rescan();
    void poll();
    const GY271Telemetry &telemetry() const { return telemetry_; }

private:
    TwoWire *wire_ = nullptr;
    GY271Telemetry telemetry_{};
    uint32_t lastGoodMs_ = 0;
    uint32_t lastScanMs_ = 0;

    bool ping(uint8_t address);
    bool readRegs(uint8_t address, uint8_t firstReg, uint8_t *dst, size_t len);
    bool readReg(uint8_t address, uint8_t reg, uint8_t &value);
    bool writeReg(uint8_t address, uint8_t reg, uint8_t value);
    bool detectHmc();
    bool detectQmc();
    bool configureHmc();
    bool configureQmc();
    bool pollHmc();
    bool pollQmc();
    void publish();
    void markError();
};
#endif
