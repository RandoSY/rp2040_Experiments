#include "gy271_sensor.h"
#include "gy271_math.h"
#include <string.h>

GY271Mailbox g_gy271Mailbox;
volatile uint32_t g_gy271RescanRequest = 0;
volatile uint32_t g_gy271RescanDone = 0;

void GY271Mailbox::publish(const GY271Telemetry &value) {
    sequence_++;
    __sync_synchronize();
    value_ = value;
    __sync_synchronize();
    sequence_++;
}

bool GY271Mailbox::snapshot(GY271Telemetry &out) const {
    for (unsigned tries=0; tries<8; ++tries) {
        uint32_t a = sequence_;
        __sync_synchronize();
        if (a & 1u) continue;
        out = value_;
        __sync_synchronize();
        uint32_t b = sequence_;
        if (a == b && !(b & 1u)) return true;
    }
    return false;
}

const char *gy271ChipName(GY271Chip chip) {
    switch (chip) {
        case GY271Chip::HMC5883L: return "HMC5883L";
        case GY271Chip::QMC5883L: return "QMC5883L";
        default: return "none";
    }
}

#ifdef ARDUINO

static constexpr uint8_t HMC_ADDR = 0x1E;
static constexpr uint8_t QMC_ADDR = 0x0D;

bool GY271Sensor::begin(TwoWire &wire, int sdaPin, int sclPin) {
    wire_ = &wire;
    wire_->setSDA(sdaPin);
    wire_->setSCL(sclPin);
    wire_->begin();
    wire_->setClock(400000);
    delay(20);
    return rescan();
}

bool GY271Sensor::ping(uint8_t address) {
    wire_->beginTransmission(address);
    return wire_->endTransmission() == 0;
}

bool GY271Sensor::readRegs(uint8_t address, uint8_t firstReg, uint8_t *dst, size_t len) {
    if (!wire_ || !dst || !len) return false;
    wire_->beginTransmission(address);
    wire_->write(firstReg);
    if (wire_->endTransmission(false) != 0) return false;
    size_t got = wire_->requestFrom((int)address, (int)len);
    if (got != len) {
        while (wire_->available()) (void)wire_->read();
        return false;
    }
    for (size_t i=0;i<len;i++) {
        int c = wire_->read();
        if (c < 0) return false;
        dst[i] = (uint8_t)c;
    }
    return true;
}

bool GY271Sensor::readReg(uint8_t address, uint8_t reg, uint8_t &value) {
    return readRegs(address, reg, &value, 1);
}

bool GY271Sensor::writeReg(uint8_t address, uint8_t reg, uint8_t value) {
    wire_->beginTransmission(address);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
}

bool GY271Sensor::detectHmc() {
    if (!ping(HMC_ADDR)) return false;
    uint8_t id[3]{};
    if (!readRegs(HMC_ADDR, 0x0A, id, sizeof(id))) return false;
    if (id[0] != 0x48 || id[1] != 0x34 || id[2] != 0x33) return false;
    telemetry_.chip = GY271Chip::HMC5883L;
    telemetry_.address = HMC_ADDR;
    return configureHmc();
}

bool GY271Sensor::detectQmc() {
    if (!ping(QMC_ADDR)) return false;
    uint8_t id = 0;
    if (!readReg(QMC_ADDR, 0x0D, id) || id != 0xFF) return false;
    telemetry_.chip = GY271Chip::QMC5883L;
    telemetry_.address = QMC_ADDR;
    return configureQmc();
}

bool GY271Sensor::configureHmc() {
    if (!writeReg(HMC_ADDR, 0x00, 0x78)) return false;
    if (!writeReg(HMC_ADDR, 0x01, 0x20)) return false;
    if (!writeReg(HMC_ADDR, 0x02, 0x00)) return false;
    delay(10);
    telemetry_.configured = true;
    return true;
}

bool GY271Sensor::configureQmc() {
    if (!writeReg(QMC_ADDR, 0x0A, 0x80)) return false;
    delay(10);
    if (!writeReg(QMC_ADDR, 0x0B, 0x01)) return false;
    if (!writeReg(QMC_ADDR, 0x09, 0x09)) return false;
    delay(10);
    telemetry_.configured = true;
    return true;
}

void GY271Sensor::publish() {
    g_gy271Mailbox.publish(telemetry_);
}

void GY271Sensor::markError() {
    telemetry_.errorCount++;
    uint32_t now = millis();
    if (lastGoodMs_ == 0 || (uint32_t)(now-lastGoodMs_) > 1000u) telemetry_.valid = false;
    publish();
}

bool GY271Sensor::rescan() {
    telemetry_ = GY271Telemetry{};
    lastGoodMs_ = 0;
    lastScanMs_ = millis();
    bool ok = detectHmc();
    if (!ok) {
        telemetry_ = GY271Telemetry{};
        ok = detectQmc();
    }
    if (!ok) telemetry_ = GY271Telemetry{};
    publish();
    return ok;
}

bool GY271Sensor::pollHmc() {
    uint8_t status = 0;
    if (!readReg(HMC_ADDR, 0x09, status)) return false;
    if (!(status & 0x01)) return true;

    uint8_t b[6]{};
    if (!readRegs(HMC_ADDR, 0x03, b, sizeof(b))) return false;
    int16_t x = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
    int16_t z = (int16_t)(((uint16_t)b[2] << 8) | b[3]);
    int16_t y = (int16_t)(((uint16_t)b[4] << 8) | b[5]);
    bool ov = (x == -4096 || y == -4096 || z == -4096);
    telemetry_.overflow = ov;
    telemetry_.rawX = x; telemetry_.rawY = y; telemetry_.rawZ = z;
    if (!ov) {
        telemetry_.milliUtX = gy271_hmc_milli_ut(x);
        telemetry_.milliUtY = gy271_hmc_milli_ut(y);
        telemetry_.milliUtZ = gy271_hmc_milli_ut(z);
        telemetry_.valid = true;
        telemetry_.sampleCount++;
        telemetry_.lastSampleMs = millis();
        lastGoodMs_ = telemetry_.lastSampleMs;
    }
    publish();
    return true;
}

bool GY271Sensor::pollQmc() {
    uint8_t status = 0;
    if (!readReg(QMC_ADDR, 0x06, status)) return false;
    telemetry_.overflow = (status & 0x02) != 0;
    if (!(status & 0x01)) {
        publish();
        return true;
    }

    uint8_t b[6]{};
    if (!readRegs(QMC_ADDR, 0x00, b, sizeof(b))) return false;
    int16_t x = (int16_t)(((uint16_t)b[1] << 8) | b[0]);
    int16_t y = (int16_t)(((uint16_t)b[3] << 8) | b[2]);
    int16_t z = (int16_t)(((uint16_t)b[5] << 8) | b[4]);
    telemetry_.rawX = x; telemetry_.rawY = y; telemetry_.rawZ = z;
    if (!telemetry_.overflow) {
        telemetry_.milliUtX = gy271_qmc_milli_ut(x);
        telemetry_.milliUtY = gy271_qmc_milli_ut(y);
        telemetry_.milliUtZ = gy271_qmc_milli_ut(z);
        telemetry_.valid = true;
        telemetry_.sampleCount++;
        telemetry_.lastSampleMs = millis();
        lastGoodMs_ = telemetry_.lastSampleMs;
    }
    publish();
    return true;
}

void GY271Sensor::poll() {
    uint32_t request = g_gy271RescanRequest;
    if (request != g_gy271RescanDone) {
        (void)rescan();
        g_gy271RescanDone = request;
    }

    if (!telemetry_.configured) {
        uint32_t now = millis();
        if ((uint32_t)(now-lastScanMs_) >= 2000u) (void)rescan();
        return;
    }

    bool ok = false;
    if (telemetry_.chip == GY271Chip::HMC5883L) ok = pollHmc();
    else if (telemetry_.chip == GY271Chip::QMC5883L) ok = pollQmc();
    if (!ok) markError();

    uint32_t now = millis();
    if (lastGoodMs_ && (uint32_t)(now-lastGoodMs_) > 1000u && telemetry_.valid) {
        telemetry_.valid = false;
        publish();
    }
}

#endif
