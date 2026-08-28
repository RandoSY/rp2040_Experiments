#pragma once
#include <stdint.h>
#include <stddef.h>

class CDP1802Bus {
public:
    virtual ~CDP1802Bus() = default;
    virtual uint8_t read(uint16_t address) = 0;
    virtual void write(uint16_t address, uint8_t value) = 0;
    virtual uint8_t input(uint8_t port) { (void)port; return 0; }
    virtual void output(uint8_t port, uint8_t value) { (void)port; (void)value; }
    virtual void qChanged(bool q) { (void)q; }
};

struct CDP1802Step {
    uint16_t pc = 0;
    uint8_t opcode = 0;
    uint8_t cycles = 0;       // 1802 machine cycles (8 input clocks each)
    bool idle = false;
    bool halted = false;
    bool interrupt = false;
};

// Complete architecturally visible state plus emulator bookkeeping.
// This is intentionally a plain data structure so host-side regression tools
// can compare the RP2040 implementation with an independent 1802 model.
struct CDP1802State {
    uint16_t R[16]{};
    uint8_t D = 0;
    uint8_t DF = 0;
    uint8_t P = 0;
    uint8_t X = 0;
    uint8_t T = 0;
    uint8_t IE = 1;
    uint8_t Q = 0;
    bool EF[4]{};
    bool idle = false;
    bool halted = false;
    bool pendingInterrupt = false;
    uint8_t undefinedOpcode = 0;
    uint64_t instructions = 0;
    uint64_t machineCycles = 0;
};

class CDP1802 {
public:
    explicit CDP1802(CDP1802Bus &bus);

    uint16_t R[16]{};
    uint8_t D = 0;
    uint8_t DF = 0;
    uint8_t P = 0;
    uint8_t X = 0;
    uint8_t T = 0;
    uint8_t IE = 1;
    uint8_t Q = 0;
    bool EF[4]{};

    bool idle = false;
    bool halted = false;
    bool trapUndefined = true;
    bool pendingInterrupt = false;
    uint8_t undefinedOpcode = 0;
    uint64_t instructions = 0;
    uint64_t machineCycles = 0;

    void hardReset();
    void reset1802();
    CDP1802Step step();
    bool interrupt();
    void requestInterrupt() { pendingInterrupt = true; }
    void setEF(unsigned n, bool asserted);
    uint16_t pc() const { return R[P & 0x0f]; }
    void setPC(uint16_t value) { R[P & 0x0f] = value; }

    CDP1802State snapshot() const;
    void restore(const CDP1802State &state);

private:
    CDP1802Bus &bus_;

    uint8_t fetch();
    uint8_t imm();
    void setQ(bool value);
    void add(uint8_t a, uint8_t b, bool carry);
    void sub(uint8_t minuend, uint8_t subtrahend, bool borrow);
    void shortBranch(bool condition);
    void longBranch(bool condition);
    void longSkip(bool condition);
    bool condShort(uint8_t n) const;
    bool condLong(uint8_t n) const;
    bool condLongSkip(uint8_t n) const;
};
