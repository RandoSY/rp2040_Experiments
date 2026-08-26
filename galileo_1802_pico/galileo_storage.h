#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include "galileo_machine.h"

class GalileoImageStore {
public:
    bool begin();
    bool haveRom() const;
    bool haveRam() const;
    bool load(GalileoMachine &machine, Print *log=nullptr);
    bool saveRom(const uint8_t *data, size_t len, Print *log=nullptr);
    bool saveRam(const uint8_t *data, size_t len, Print *log=nullptr);
    bool eraseAll(Print *log=nullptr);
private:
    bool saveFile(const char *path, const uint8_t *data, size_t len, Print *log);
};
#endif
