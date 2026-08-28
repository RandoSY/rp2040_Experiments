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

static void test_reset_and_state_visibility() {
    TestBus b; CDP1802 c(b);
    c.R[3] = 0x3344;
    c.D = 0x55;
    c.DF = 1;
    c.T = 0xA5;
    c.P = 3;
    c.X = 7;
    c.IE = 0;
    c.setEF(1, true);
    c.setEF(4, true);
    c.halted = true;
    c.idle = true;
    c.pendingInterrupt = true;
    c.undefinedOpcode = 0x68;
    c.instructions = 123;
    c.machineCycles = 456;

    CDP1802State before = c.snapshot();
    CHECK(before.R[3] == 0x3344 && before.D == 0x55 && before.DF == 1,
          "snapshot captures registers and arithmetic state");
    CHECK(before.P == 3 && before.X == 7 && before.T == 0xA5 && before.IE == 0,
          "snapshot captures selectors/T/IE");
    CHECK(before.EF[0] && before.EF[3] && !before.EF[1] && !before.EF[2],
          "snapshot captures EF lines");
    CHECK(before.halted && before.idle && before.pendingInterrupt && before.undefinedOpcode == 0x68,
          "snapshot captures emulator execution state");
    CHECK(before.instructions == 123 && before.machineCycles == 456,
          "snapshot captures counters");

    c.hardReset();
    CHECK(c.R[0] == 0 && c.P == 0 && c.X == 0 && c.IE == 1 && c.Q == 0,
          "hardReset establishes defined CDP1802 reset state");
    CHECK(!c.idle && !c.halted && !c.pendingInterrupt && c.undefinedOpcode == 0,
          "hardReset clears execution traps/waits");
    CHECK(c.instructions == 0 && c.machineCycles == 0,
          "hardReset clears counters");

    c.restore(before);
    CDP1802State after = c.snapshot();
    CHECK(memcmp(before.R, after.R, sizeof(before.R)) == 0,
          "restore round-trips all R registers");
    CHECK(after.D == before.D && after.DF == before.DF && after.P == before.P &&
          after.X == before.X && after.T == before.T && after.IE == before.IE && after.Q == before.Q,
          "restore round-trips scalar CPU state");
    CHECK(memcmp(before.EF, after.EF, sizeof(before.EF)) == 0,
          "restore round-trips EF state");
    CHECK(after.idle == before.idle && after.halted == before.halted &&
          after.pendingInterrupt == before.pendingInterrupt && after.undefinedOpcode == before.undefinedOpcode,
          "restore round-trips execution state");
    CHECK(after.instructions == before.instructions && after.machineCycles == before.machineCycles,
          "restore round-trips counters");
}

static void test_register_families() {
    TestBus b; CDP1802 c(b);

    for (unsigned n = 0; n < 16; n++) {
        c.hardReset();
        // Keep the program counter in a different register from the register
        // under test. R0 is the PC after reset, so testing R0 directly without
        // this isolation would move the instruction fetch itself.
        const unsigned pcReg = (n == 15) ? 14 : 15;
        c.P = (uint8_t)pcReg;
        c.R[pcReg] = 0;
        c.R[n] = (uint16_t)(0x1200u | n);

        b.m[0] = (uint8_t)(0x10u | n); // INC
        c.step();
        CHECK(c.R[n] == (uint16_t)(0x1201u | n), "INC Rn");

        c.setPC(0);
        b.m[0] = (uint8_t)(0x20u | n); // DEC
        c.step();
        CHECK(c.R[n] == (uint16_t)(0x1200u | n), "DEC Rn");

        c.setPC(0);
        b.m[0] = (uint8_t)(0x80u | n); // GLO
        c.step();
        CHECK(c.D == (uint8_t)n, "GLO Rn");

        c.setPC(0);
        b.m[0] = (uint8_t)(0x90u | n); // GHI
        c.step();
        CHECK(c.D == 0x12, "GHI Rn");

        c.D = 0xA5;
        c.setPC(0);
        b.m[0] = (uint8_t)(0xA0u | n); // PLO
        c.step();
        CHECK((c.R[n] & 0x00ffu) == 0xA5, "PLO Rn");

        c.D = 0x5A;
        c.setPC(0);
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

    c.setPC(0); b.m[0] = 0x43; // LDA R3
    c.step();
    CHECK(c.D == 0x55 && c.R[3] == 0x2001, "LDA reads and advances Rn");

    c.D = 0xA6; c.setPC(0); b.m[0] = 0x53; // STR R3
    c.step();
    CHECK(b.m[0x2001] == 0xA6 && c.R[3] == 0x2001, "STR stores without advancing Rn");

    c.X = 4; c.R[4] = 0x2100; b.m[0x2100] = 0x31; c.setPC(0); b.m[0] = 0x72; // LDXA
    c.step();
    CHECK(c.D == 0x31 && c.R[4] == 0x2101, "LDXA loads and advances RX");

    c.D = 0x77; c.setPC(0); b.m[0] = 0x73; // STXD
    c.step();
    CHECK(b.m[0x2101] == 0x77 && c.R[4] == 0x2100, "STXD stores and decrements RX");

    c.setPC(0); b.m[0] = 0x60; // IRX
    c.step();
    CHECK(c.R[4] == 0x2101, "IRX advances RX");
}

static void test_arithmetic_and_shifts() {
    TestBus b; CDP1802 c(b);
    c.X = 2; c.R[2] = 0x3000;

    c.D = 0xFF; b.m[0x3000] = 0x01; b.m[0] = 0xF4; // ADD
    c.step();
    CHECK(c.D == 0x00 && c.DF == 1, "ADD carry");

    c.setPC(0); c.D = 0xFF; c.DF = 1; b.m[0] = 0x74; // ADC
    c.step();
    CHECK(c.D == 0x01 && c.DF == 1, "ADC consumes DF");

    c.setPC(0); c.D = 0x02; b.m[0x3000] = 0x05; b.m[0] = 0xF5; // SD M-D
    c.step();
    CHECK(c.D == 0x03 && c.DF == 1, "SD no borrow");

    c.setPC(0); c.D = 0x05; b.m[0x3000] = 0x02; b.m[0] = 0xF5; // SD -> -3
    c.step();
    CHECK(c.D == 0xFD && c.DF == 0, "SD borrow");

    c.setPC(0); c.D = 0x81; b.m[0] = 0xF6; // SHR
    c.step();
    CHECK(c.D == 0x40 && c.DF == 1, "SHR shifts into DF");

    c.setPC(0); c.D = 0x80; c.DF = 1; b.m[0] = 0x76; // SHRC
    c.step();
    CHECK(c.D == 0xC0 && c.DF == 0, "SHRC rotates old DF into bit 7");

    c.setPC(0); c.D = 0x81; b.m[0] = 0xFE; // SHL
    c.step();
    CHECK(c.D == 0x02 && c.DF == 1, "SHL shifts into DF");

    c.setPC(0); c.D = 0x40; c.DF = 1; b.m[0] = 0x7E; // SHLC
    c.step();
    CHECK(c.D == 0x81 && c.DF == 0, "SHLC rotates old DF into bit 0");
}

static void test_branch_and_skip_families() {
    TestBus b; CDP1802 c(b);

    // Short branch is page-local to the operand address.
    c.R[0] = 0x12FE; c.P = 0; b.m[0x12FE] = 0x30; b.m[0x12FF] = 0x34;
    c.step();
    CHECK(c.pc() == 0x1234, "BR uses operand page");

    c.hardReset(); c.D = 0; b.m[0] = 0x32; b.m[1] = 0x80; // BZ
    c.step(); CHECK(c.pc() == 0x0080, "BZ taken");

    c.hardReset(); c.D = 1; b.m[0] = 0x32; b.m[1] = 0x80;
    c.step(); CHECK(c.pc() == 0x0002, "BZ not taken consumes operand");

    c.hardReset(); b.m[0] = 0x38; b.m[1] = 0xAA; // SKP
    c.step(); CHECK(c.pc() == 0x0002, "SKP skips one byte");

    c.hardReset(); load(b, 0, {0xC0, 0x12, 0x34}); // LBR
    c.step(); CHECK(c.pc() == 0x1234, "LBR target");

    c.hardReset(); c.D = 1; load(b, 0, {0xC2, 0x12, 0x34}); // LBZ not taken
    c.step(); CHECK(c.pc() == 0x0003, "LBZ not taken consumes address");

    c.hardReset(); c.D = 0; load(b, 0, {0xCE, 0xAA, 0xBB, 0xF8, 0x42}); // LSZ
    c.step(); CHECK(c.pc() == 0x0003, "LSZ skips two bytes");
    c.step(); CHECK(c.D == 0x42, "LSZ landing instruction");

    c.hardReset(); c.IE = 1; load(b, 0, {0xCC, 0xAA, 0xBB, 0xF8, 0x5A}); // LSIE
    c.step(); CHECK(c.pc() == 0x0003, "LSIE skips when IE=1");
    c.step(); CHECK(c.D == 0x5A, "LSIE landing instruction");
}

static void test_q_ef_and_io() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0x7B, 0x7A}); // SEQ, REQ
    c.step(); CHECK(c.Q == 1 && b.q, "SEQ sets Q and notifies bus");
    c.step(); CHECK(c.Q == 0 && !b.q, "REQ clears Q and notifies bus");

    c.hardReset(); c.setEF(1, true); load(b, 0, {0x34, 0x20}); // B1
    c.step(); CHECK(c.pc() == 0x0020, "B1 sees asserted EF1");

    c.hardReset(); c.setEF(1, false); load(b, 0, {0x3C, 0x22}); // BN1
    c.step(); CHECK(c.pc() == 0x0022, "BN1 sees deasserted EF1");

    c.hardReset(); c.X = 2; c.R[2] = 0x4000; b.m[0x4000] = 0xA5; b.m[0] = 0x61; // OUT 1
    c.step();
    CHECK(b.outCount == 1 && b.lastOutPort == 1 && b.lastOutValue == 0xA5 && c.R[2] == 0x4001,
          "OUT reads RX, notifies port, advances RX");

    c.hardReset(); c.X = 2; c.R[2] = 0x4100; b.in[1] = 0x6C; b.m[0] = 0x69; // INP 1
    c.step();
    CHECK(c.D == 0x6C && b.m[0x4100] == 0x6C && c.R[2] == 0x4100,
          "INP stores through RX and loads D without advancing RX");
}

static void test_control_interrupt_and_cycles() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xD5, 0xE7}); // SEP 5; subsequent fetch comes from R5
    c.R[5] = 1;
    c.step(); CHECK(c.P == 5, "SEP selects P");
    c.step(); CHECK(c.X == 7, "SEX selects X");

    c.hardReset(); c.X = 2; c.R[2] = 0x5000; b.m[0x5000] = 0x34; b.m[0] = 0x70; // RET
    c.IE = 0; c.step();
    CHECK(c.X == 3 && c.P == 4 && c.R[2] == 0x5001 && c.IE == 1, "RET restores X/P and enables IE");

    c.hardReset(); c.X = 2; c.R[2] = 0x5000; b.m[0x5000] = 0x34; b.m[0] = 0x71; // DIS
    c.IE = 1; c.step();
    CHECK(c.X == 3 && c.P == 4 && c.R[2] == 0x5001 && c.IE == 0, "DIS restores X/P and disables IE");

    c.hardReset(); c.P = 5; c.X = 7; c.R[5] = 0x2345; c.IE = 1;
    CHECK(c.interrupt(), "interrupt accepted when IE=1");
    CHECK(c.T == 0x75 && c.X == 2 && c.P == 1 && c.IE == 0, "interrupt entry state");
    CHECK(!c.interrupt(), "nested interrupt blocked when IE=0");

    c.hardReset(); b.m[0] = 0xF8; b.m[1] = 0x11; // LDI
    CDP1802Step s = c.step();
    CHECK(s.cycles == 2 && c.machineCycles == 2 && c.instructions == 1, "normal opcode cycle accounting");

    c.hardReset(); load(b, 0, {0xC0, 0x12, 0x34}); // LBR
    s = c.step();
    CHECK(s.cycles == 3 && c.machineCycles == 3 && c.instructions == 1, "long opcode cycle accounting");

    c.hardReset(); b.m[0] = 0x00; // IDL
    s = c.step();
    CHECK(c.idle && s.idle && !c.halted, "IDL waits rather than halts");

    c.hardReset(); b.m[0] = 0x68; // undefined
    s = c.step();
    CHECK(c.halted && s.halted && c.undefinedOpcode == 0x68, "undefined opcode trap");
}

int main() {
    test_reset_and_state_visibility();
    test_register_families();
    test_load_store_and_indexing();
    test_arithmetic_and_shifts();
    test_branch_and_skip_families();
    test_q_ef_and_io();
    test_control_interrupt_and_cycles();

    printf("CDP1802 ISA regression: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
