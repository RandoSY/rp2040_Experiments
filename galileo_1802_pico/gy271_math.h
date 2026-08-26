#pragma once
#include <stdint.h>

// Magnetic-field conversion helpers.  Returned units are milli-microtesla
// (0.001 uT), so the 1802/MMIO path does not need floating point.
//
// HMC5883L: default GN=001, 1090 LSB/gauss.
// QMC5883L: +/-2 gauss range, 12000 LSB/gauss.
// 1 gauss = 100 uT.
inline int32_t gy271_hmc_milli_ut(int16_t raw) {
    return (int32_t)(((int64_t)raw * 100000LL) / 1090LL);
}
inline int32_t gy271_qmc_milli_ut(int16_t raw) {
    return (int32_t)(((int64_t)raw * 100000LL) / 12000LL);
}
