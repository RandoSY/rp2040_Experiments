#include "../cdp1802.h"
#include <initializer_list>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct TestBus : CDP1802Bus {
    uint8_t m[65536]{};
    uint8_t in[8]{};
    uint8_t lastOutPort = 0;
    uint8_t lastOutValue = 0;
    unsigned outCount = 0;
    bool q = false;
    unsigned qChanges = 0;

    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
    uint8_t input(uint8_t port) override { return in[port & 7]; }
    void output(uint8_t port, uint8_t value) override {
        lastOutPort = port;
        lastOutValue = value;
        outCount++;
    }
    void qChanged(bool value) override {
        q = value;
        qChanges++;
    }
};

static int passed = 0;
static int failed = 0;

#define CHECK(expr, msg) do { \
    if (expr) { passed++; } \
    else { failed++; fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

static void load(TestBus &b, uint16_t address, std::initializer_list<uint8_t> bytes) {
    for (uint8_t v : bytes) b.m[address++] = v;
}

static void stepN(CDP1802 &c, unsigned n) {
    while (n--) c.step();
}

static void test_register_families() {
    TestBus b; CDP1802 c(b);

    for (unsigned n = 0; n < 16; n++) {
        c.hardReset();
        const unsigned p = (n == 0) ? 1 : 0; // Keep the program counter in a different R register.
        const uint16_t initial = (uint16_t)(0x1200u + n);
        c.P = (uint8_t)p;
        c.R[p] = 0;
        c.R[n] = initial;

        b.m[0] = (uint8_t)(0x10u | n); // INC
        c.step();
        CHECK(c.R[n] == (uint16_t)(initial + 1), "INC Rn");

        c.R[p] = 0;
        b.m[0] = (uint8_t)(0x20u | n); // DEC
        c.step();
        CHECK(c.R[n] == initial, "DEC Rn");

        c.R[p] = 0;
        b.m[0] = (uint8_t)(0x80u | n); // GLO
        c.step();
        CHECK(c.D == (uint8_t)n, "GLO Rn");

        c.R[p] = 0;
        b.m[0] = (uint8_t)(0x90u | n); // GHI
        c.step();
        CHECK(c.D == 0x12, "GHI Rn");

        c.D = 0xA5;
        c.R[p] = 0;
        b.m[0] = (uint8_t)(0xA0u | n); // PLO
        c.step();
        CHECK((c.R[n] & 0x00ffu) == 0xA5, "PLO Rn");

        c.D = 0x5A;
        c.R[p] = 0;
        b.m[0] = (uint8_t)(0xB0u | n); // PHI
        c.step();
        CHECK((c.R[n] >> 8) == 0x5A, "PHI Rn");
    }
}

static void test_load_store_and_indexing() {
    TestBus b; CDP1802 c(b);
    c.R[3] = 0x2000;
    b.m[0x2000] = 0x55;

    b.m[0] = 0x03; // LDN R3
    c.step();
    CHECK(c.D == 0x55 && c.R[3] == 0x2000, "LDN does not advance Rn");

    c.setPC(0);
    b.m[0] = 0x43; // LDA R3
    c.step();
    CHECK(c.D == 0x55 && c.R[3] == 0x2001, "LDA advances Rn");

    c.D = 0xA6;
    c.R[4] = 0x2100;
    c.setPC(0);
    b.m[0] = 0x54; // STR R4
    c.step();
    CHECK(b.m[0x2100] == 0xA6 && c.R[4] == 0x2100, "STR stores without advancing Rn");

    c.X = 5;
    c.R[5] = 0x2200;
    b.m[0x2200] = 0x7E;
    c.setPC(0); b.m[0] = 0x72; // LDXA
    c.step();
    CHECK(c.D == 0x7E && c.R[5] == 0x2201, "LDXA loads and advances X");

    c.D = 0x33;
    c.setPC(0); b.m[0] = 0x73; // STXD
    c.step();
    CHECK(b.m[0x2201] == 0x33 && c.R[5] == 0x2200, "STXD stores and decrements X");

    c.setPC(0); b.m[0] = 0x60; // IRX
    c.step();
    CHECK(c.R[5] == 0x2201, "IRX increments X register");
}

static void test_logic_and_arithmetic() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xF8,0x3C, 0xF9,0x0F, 0xFA,0x3F, 0xFB,0x33});
    stepN(c, 4);
    CHECK(c.D == 0x0C, "ORI/ANI/XRI immediate logic");

    c.hardReset();
    load(b, 0, {0xF8,0xFF, 0xFC,0x01});
    stepN(c, 2);
    CHECK(c.D == 0x00 && c.DF == 1, "ADI carry out");

    c.hardReset();
    load(b, 0, {0xF8,0x01, 0xF6, 0xF8,0x00, 0x7C,0xFF});
    stepN(c, 4);
    CHECK(c.D == 0x00 && c.DF == 1, "ADCI consumes DF carry");

    c.hardReset();
    load(b, 0, {0xF8,0x10, 0xFD,0x20}); // SDI: immediate - D
    stepN(c, 2);
    CHECK(c.D == 0x10 && c.DF == 1, "SDI subtract-D ordering");

    c.hardReset();
    load(b, 0, {0xF8,0x10, 0xFF,0x20}); // SMI: D - immediate
    stepN(c, 2);
    CHECK(c.D == 0xF0 && c.DF == 0, "SMI borrow convention");

    c.hardReset();
    load(b, 0, {0xF8,0x00, 0xF6, 0xF8,0x10, 0x7D,0x20}); // DF=0 then SDBI
    stepN(c, 4);
    CHECK(c.D == 0x0F && c.DF == 1, "SDBI borrow-in when DF=0");

    c.hardReset();
    load(b, 0, {0xF8,0x00, 0xF6, 0xF8,0x10, 0x7F,0x20}); // DF=0 then SMBI
    stepN(c, 4);
    CHECK(c.D == 0xEF && c.DF == 0, "SMBI borrow-in when DF=0");
}

static void test_shifts() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xF8,0x55, 0xF6}); // SHR
    stepN(c, 2);
    CHECK(c.D == 0x2A && c.DF == 1, "SHR moves bit 0 to DF");

    c.hardReset();
    load(b, 0, {0xF8,0x80, 0xFE}); // SHL
    stepN(c, 2);
    CHECK(c.D == 0x00 && c.DF == 1, "SHL moves bit 7 to DF");

    c.hardReset();
    load(b, 0, {0xF8,0x01, 0xF6, 0xF8,0x02, 0x76}); // DF=1, SHRC
    stepN(c, 4);
    CHECK(c.D == 0x81 && c.DF == 0, "SHRC rotates old DF into bit 7");

    c.hardReset();
    load(b, 0, {0xF8,0x01, 0xF6, 0xF8,0x80, 0x7E}); // DF=1, SHLC
    stepN(c, 4);
    CHECK(c.D == 0x01 && c.DF == 1, "SHLC rotates old DF into bit 0");
}

static void test_short_branches() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0x7B, 0x31,0x08, 0xF8,0x11, 0x30,0x0A, 0x00, 0xF8,0x22});
    c.step(); c.step();
    CHECK(c.pc() == 0x0008, "BQ taken when Q=1");
    c.step();
    CHECK(c.D == 0x22, "BQ target executes");

    c.hardReset(); c.setEF(1, true);
    load(b, 0, {0x34,0x05, 0xF8,0x11, 0x00, 0xF8,0x33});
    c.step();
    CHECK(c.pc() == 0x0005, "B1 taken for asserted EF1");
    c.step();
    CHECK(c.D == 0x33, "B1 target executes");

    c.hardReset(); c.setEF(1, false);
    load(b, 0, {0x3C,0x05, 0xF8,0x11, 0x00, 0xF8,0x44});
    c.step();
    CHECK(c.pc() == 0x0005, "BN1 taken for deasserted EF1");

    c.hardReset(); c.setPC(0x00FF);
    b.m[0x00FF] = 0x30; b.m[0x0100] = 0x42;
    c.step();
    CHECK(c.pc() == 0x0142, "short branch uses page containing operand byte");
}

static void test_long_branches_and_skips() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xF8,0x00, 0xC2,0x12,0x34});
    c.step(); c.step();
    CHECK(c.pc() == 0x1234, "LBZ long branch taken");

    c.hardReset(); c.IE = 1;
    load(b, 0, {0xCC,0xAA,0xBB, 0xF8,0x42});
    c.step();
    CHECK(c.pc() == 3, "LSIE skips two bytes");
    c.step();
    CHECK(c.D == 0x42, "LSIE lands after skipped bytes");

    c.hardReset(); c.Q = 0;
    load(b, 0, {0xC5,0xAA,0xBB, 0xF8,0x66});
    c.step();
    CHECK(c.pc() == 3, "LSNQ condition");
}

static void test_io_q_and_undefined() {
    TestBus b; CDP1802 c(b);

    c.X = 2; c.R[2] = 0x1000; b.m[0x1000] = 0xA5; b.m[0] = 0x61;
    c.step();
    CHECK(b.outCount == 1 && b.lastOutPort == 1 && b.lastOutValue == 0xA5, "OUT sends M(RX) to port");
    CHECK(c.R[2] == 0x1001, "OUT advances R(X)");

    c.hardReset(); c.X = 2; c.R[2] = 0x1100; b.in[1] = 0x5A; b.m[0] = 0x69;
    c.step();
    CHECK(c.D == 0x5A && b.m[0x1100] == 0x5A, "INP copies port to D and M(RX)");
    CHECK(c.R[2] == 0x1100, "INP leaves R(X) unchanged");

    c.hardReset(); load(b, 0, {0x7B,0x7A});
    c.step(); CHECK(c.Q == 1 && b.q, "SEQ sets Q and notifies bus");
    c.step(); CHECK(c.Q == 0 && !b.q, "REQ clears Q and notifies bus");

    c.hardReset(); b.m[0] = 0x68; c.step();
    CHECK(c.halted && c.undefinedOpcode == 0x68, "undefined 68 traps when enabled");
}

static void test_control_state_and_cycles() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xF8,0x12, 0xC4, 0x00});
    CDP1802Step a = c.step();
    CDP1802Step n = c.step();
    CDP1802Step i = c.step();
    CHECK(a.cycles == 2 && n.cycles == 3 && i.cycles == 2, "instruction cycle counts 2/3/2");
    CHECK(c.machineCycles == 7 && c.instructions == 3, "cycle/instruction accumulators");
    CHECK(c.idle, "IDL enters idle state");

    c.R[5] = 0x1234; c.P = 5; c.X = 7; c.IE = 1; c.idle = true;
    uint64_t before = c.machineCycles;
    CHECK(c.interrupt(), "interrupt accepted when IE=1");
    CHECK(c.T == 0x75 && c.P == 1 && c.X == 2 && c.IE == 0 && !c.idle, "interrupt saves XP and selects R1/R2");
    CHECK(c.machineCycles == before + 1, "interrupt charges one machine cycle");
}

static void test_snapshot_restore() {
    TestBus b; CDP1802 c(b);
    for (unsigned i = 0; i < 16; i++) c.R[i] = (uint16_t)(0x1000u + i * 0x111u);
    c.D = 0xA5; c.DF = 1; c.P = 7; c.X = 9; c.T = 0x97; c.IE = 0; c.setEF(1,true); c.setEF(4,true);
    c.instructions = 1234; c.machineCycles = 5678; c.requestInterrupt();
    load(b, c.pc(), {0x7B}); c.step();

    CDP1802State saved = c.snapshot();
    c.hardReset();
    c.restore(saved);
    CDP1802State again = c.snapshot();

    CHECK(memcmp(saved.R, again.R, sizeof(saved.R)) == 0, "snapshot restores R0-RF");
    CHECK(saved.D == again.D && saved.DF == again.DF && saved.P == again.P && saved.X == again.X && saved.T == again.T, "snapshot restores scalar registers");
    CHECK(saved.IE == again.IE && saved.Q == again.Q && saved.EF[0] == again.EF[0] && saved.EF[3] == again.EF[3], "snapshot restores flags and external flags");
    CHECK(saved.instructions == again.instructions && saved.machineCycles == again.machineCycles, "snapshot restores counters");
}

int main() {
    test_register_families();
    test_load_store_and_indexing();
    test_logic_and_arithmetic();
    test_shifts();
    test_short_branches();
    test_long_branches_and_skips();
    test_io_q_and_undefined();
    test_control_state_and_cycles();
    test_snapshot_restore();

    printf("CDP1802 regression: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
