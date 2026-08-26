#include "crc32.h"
uint32_t crc32_ieee(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i=0;i<len;i++) {
        crc ^= data[i];
        for (unsigned k=0;k<8;k++) crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc ^ 0xffffffffu;
}
