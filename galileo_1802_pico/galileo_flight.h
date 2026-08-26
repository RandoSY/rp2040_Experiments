#pragma once
#include "galileo_machine.h"
#include <stdint.h>
#include <stddef.h>

struct GalileoWord {
    const char *name;
    uint16_t printedAddress;   // address printed by preserved dictionary
    bool callable;
};

extern const GalileoWord GALILEO_WORDS[];
extern const size_t GALILEO_WORD_COUNT;
const GalileoWord *findGalileoWord(const char *name);

class GalileoFlightBridge {
public:
    explicit GalileoFlightBridge(GalileoMachine &machine) : machine_(machine) {}

    bool invoke(const GalileoWord &word, uint16_t *stack, size_t &depth, size_t capacity,
                uint32_t maxSteps=500000);
    bool runIrqSubroutine(uint16_t entry, uint32_t maxSteps=250000);
    uint32_t lastSteps() const { return lastSteps_; }
    const char *lastError() const { return lastError_; }

private:
    GalileoMachine &machine_;
    uint32_t lastSteps_ = 0;
    const char *lastError_ = "";

    bool packStack(const uint16_t *stack, size_t depth, uint16_t &sp);
    bool unpackStack(uint16_t sp, uint16_t *stack, size_t &depth, size_t capacity);
};
