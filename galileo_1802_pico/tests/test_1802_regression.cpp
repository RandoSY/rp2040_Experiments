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

    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
    uint8_t input(uint8_t port) override { return in[port & 7]; }
    void output(uint8_t port, uint8_t value) override {
        lastOutPort = port;
        lastOutValue = value;
        outCount++;
    }
    void qChanged(bool value) override { q = value; }
};

static int passed = 0;
static int failed = 0;

#define CHECK(expr, msg) do { \
    if (expr) passed++; \
    else { failed++; fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

static void load(TestBus &b, uint16_t address, std::initializer_list<uint8_t> bytes) {
    for (uint8_t v : bytes) b.m[address++] = v;
}

static void test_state_api() {
    TestBus b; CDP1802 c(b);
    c.R[3] = 0x3344; c.D = 0x55; c.DF = 1; c.P = 3; c.X = 7; c.T = 0xA5; c.IE = 0;
    c.setEF(1, true); c.setEF(4, true);
    c.idle = true; c.halted = true; c.pendingInterrupt = true; c.undefinedOpcode = 0x68;
    c.instructions = 123; c.machineCycles = 456;

    const CDP1802State before = c.snapshot();
    c.hardReset();
    CHECK(c.R[0] == 0 && c.P == 0 && c.X == 0 && c.IE == 1 && c.Q == 0,
          "hard reset defined state");
    CHECK(!c.idle && !c.halted && !c.pendingInterrupt && c.undefinedOpcode == 0,
          "hard reset execution state");
    CHECK(c.instructions == 0 && c.machineCycles == 0, "hard reset counters");

    c.restore(before);
    const CDP1802State after = c.snapshot();
    CHECK(memcmp(before.R, after.R, sizeof(before.R)) == 0, "state R round trip");
    CHECK(before.D == after.D && before.DF == after.DF && before.P == after.P &&
          before.X == after.X && before.T == after.T && before.IE == after.IE && before.Q == after.Q,
          "state scalar round trip");
    CHECK(memcmp(before.EF, after.EF, sizeof(before.EF)) == 0, "state EF round trip");
    CHECK(before.idle == after.idle && before.halted == after.halted &&
          before.pendingInterrupt == after.pendingInterrupt && before.undefinedOpcode == after.undefinedOpcode,
          "state execution round trip");
    CHECK(before.instructions == after.instructions && before.machineCycles == after.machineCycles,
          "state counters round trip");
}

static void test_register_families() {
    TestBus b; CDP1802 c(b);
    for (unsigned n = 0; n < 16; n++) {
        c.hardReset();
        const unsigned pcReg = (n == 15) ? 14 : 15;
        c.P = (uint8_t)pcReg;
        c.R[pcReg] = 0;
        c.R[n] = (uint16_t)(0x1200u + n);

        b.m[0] = (uint8_t)(0x10u | n); c.step();
        CHECK(c.R[n] == (uint16_t)(0x1201u + n), "INC Rn");

        c.setPC(0); b.m[0] = (uint8_t)(0x20u | n); c.step();
        CHECK(c.R[n] == (uint16_t)(0x1200u + n), "DEC Rn");

        c.setPC(0); b.m[0] = (uint8_t)(0x80u | n); c.step();
        CHECK(c.D == (uint8_t)n, "GLO Rn");

        c.setPC(0); b.m[0] = (uint8_t)(0x90u | n); c.step();
        CHECK(c.D == 0x12, "GHI Rn");

        c.D = 0xA5; c.setPC(0); b.m[0] = (uint8_t)(0xA0u | n); c.step();
        CHECK((c.R[n] & 0x00ffu) == 0xA5, "PLO Rn");

        c.D = 0x5A; c.setPC(0); b.m[0] = (uint8_t)(0xB0u | n); c.step();
        CHECK(c.R[n] == 0x5AA5, "PHI Rn");
    }
}

static void test_memory_and_indexing() {
    TestBus b; CDP1802 c(b);
    c.R[3] = 0x2000; b.m[0x2000] = 0x55;

    b.m[0] = 0x03; c.step();
    CHECK(c.D == 0x55 && c.R[3] == 0x2000, "LDN");

    c.setPC(0); b.m[0] = 0x43; c.step();
    CHECK(c.D == 0x55 && c.R[3] == 0x2001, "LDA");

    c.D = 0xA6; c.setPC(0); b.m[0] = 0x53; c.step();
    CHECK(b.m[0x2001] == 0xA6 && c.R[3] == 0x2001, "STR");

    c.X = 4; c.R[4] = 0x2100; b.m[0x2100] = 0x31;
    c.setPC(0); b.m[0] = 0x72; c.step();
    CHECK(c.D == 0x31 && c.R[4] == 0x2101, "LDXA");

    c.D = 0x77; c.setPC(0); b.m[0] = 0x73; c.step();
    CHECK(b.m[0x2101] == 0x77 && c.R[4] == 0x2100, "STXD");

    c.setPC(0); b.m[0] = 0x60; c.step();
    CHECK(c.R[4] == 0x2101, "IRX");
}

static void test_arithmetic_logic_and_shifts() {
    TestBus b; CDP1802 c(b);

    c.D = 0xFF; load(b, 0, {0xFC, 0x01}); c.step();
    CHECK(c.D == 0x00 && c.DF == 1, "ADI carry");

    c.hardReset(); c.D = 0xFF; c.DF = 1; load(b, 0, {0x7C, 0x01}); c.step();
    CHECK(c.D == 0x01 && c.DF == 1, "ADCI carry in/out");

    c.hardReset(); c.D = 0x02; load(b, 0, {0xFD, 0x05}); c.step();
    CHECK(c.D == 0x03 && c.DF == 1, "SDI no borrow");

    c.hardReset(); c.D = 0x05; load(b, 0, {0xFD, 0x02}); c.step();
    CHECK(c.D == 0xFD && c.DF == 0, "SDI borrow");

    c.hardReset(); c.D = 0xA5; load(b, 0, {0xF9, 0x0F, 0xFA, 0xF3, 0xFB, 0x55});
    c.step(); CHECK(c.D == 0xAF, "ORI");
    c.step(); CHECK(c.D == 0xA3, "ANI");
    c.step(); CHECK(c.D == 0xF6, "XRI");

    c.hardReset(); c.D = 0x81; b.m[0] = 0xF6; c.step();
    CHECK(c.D == 0x40 && c.DF == 1, "SHR");

    c.hardReset(); c.D = 0x80; c.DF = 1; b.m[0] = 0x76; c.step();
    CHECK(c.D == 0xC0 && c.DF == 0, "SHRC");

    c.hardReset(); c.D = 0x81; b.m[0] = 0xFE; c.step();
    CHECK(c.D == 0x02 && c.DF == 1, "SHL");

    c.hardReset(); c.D = 0x40; c.DF = 1; b.m[0] = 0x7E; c.step();
    CHECK(c.D == 0x81 && c.DF == 0, "SHLC");
}

static void test_branches_and_skips() {
    TestBus b; CDP1802 c(b);

    c.R[0] = 0x12FE; b.m[0x12FE] = 0x30; b.m[0x12FF] = 0x34; c.step();
    CHECK(c.pc() == 0x1234, "BR page-local target");

    c.hardReset(); c.D = 0; load(b, 0, {0x32, 0x80}); c.step();
    CHECK(c.pc() == 0x0080, "BZ taken");

    c.hardReset(); c.D = 1; load(b, 0, {0x32, 0x80}); c.step();
    CHECK(c.pc() == 0x0002, "BZ not taken");

    c.hardReset(); load(b, 0, {0x38, 0xAA}); c.step();
    CHECK(c.pc() == 0x0002, "SKP");

    c.hardReset(); load(b, 0, {0xC0, 0x12, 0x34}); c.step();
    CHECK(c.pc() == 0x1234, "LBR");

    c.hardReset(); c.D = 0; load(b, 0, {0xCE, 0xAA, 0xBB, 0xF8, 0x42}); c.step();
    CHECK(c.pc() == 0x0003, "LSZ"); c.step(); CHECK(c.D == 0x42, "LSZ landing");

    c.hardReset(); c.IE = 1; load(b, 0, {0xCC, 0xAA, 0xBB, 0xF8, 0x5A}); c.step();
    CHECK(c.pc() == 0x0003, "LSIE"); c.step(); CHECK(c.D == 0x5A, "LSIE landing");
}

static void test_q_ef_io() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0x7B, 0x7A}); c.step();
    CHECK(c.Q == 1 && b.q, "SEQ"); c.step(); CHECK(c.Q == 0 && !b.q, "REQ");

    c.hardReset(); c.setEF(1, true); load(b, 0, {0x34, 0x20}); c.step();
    CHECK(c.pc() == 0x0020, "B1 asserted");

    c.hardReset(); c.setEF(1, false); load(b, 0, {0x3C, 0x22}); c.step();
    CHECK(c.pc() == 0x0022, "BN1 deasserted");

    c.hardReset(); c.X = 2; c.R[2] = 0x4000; b.m[0x4000] = 0xA5; b.m[0] = 0x61; c.step();
    CHECK(b.outCount == 1 && b.lastOutPort == 1 && b.lastOutValue == 0xA5 && c.R[2] == 0x4001,
          "OUT 1");

    c.hardReset(); c.X = 2; c.R[2] = 0x4100; b.in[1] = 0x6C; b.m[0] = 0x69; c.step();
    CHECK(c.D == 0x6C && b.m[0x4100] == 0x6C && c.R[2] == 0x4100, "INP 1");
}

static void test_control_interrupt_cycles() {
    TestBus b; CDP1802 c(b);

    load(b, 0, {0xD5, 0xE7}); c.R[5] = 1; c.step();
    CHECK(c.P == 5, "SEP"); c.step(); CHECK(c.X == 7, "SEX");

    c.hardReset(); c.X = 2; c.R[2] = 0x5000; b.m[0x5000] = 0x34; b.m[0] = 0x70; c.IE = 0; c.step();
    CHECK(c.X == 3 && c.P == 4 && c.R[2] == 0x5001 && c.IE == 1, "RET");

    c.hardReset(); c.X = 2; c.R[2] = 0x5000; b.m[0x5000] = 0x34; b.m[0] = 0x71; c.IE = 1; c.step();
    CHECK(c.X == 3 && c.P == 4 && c.R[2] == 0x5001 && c.IE == 0, "DIS");

    c.hardReset(); c.P = 5; c.X = 7; c.R[5] = 0x2345; c.IE = 1; c.idle = true;
    CHECK(c.interrupt(), "interrupt accepted");
    CHECK(c.T == 0x75 && c.X == 2 && c.P == 1 && c.IE == 0 && !c.idle, "interrupt state");
    CHECK(!c.interrupt(), "nested interrupt blocked");

    c.hardReset(); load(b, 0, {0xF8, 0x11}); CDP1802Step s = c.step();
    CHECK(s.cycles == 2 && c.machineCycles == 2 && c.instructions == 1, "normal cycles");

    c.hardReset(); load(b, 0, {0xC0, 0x12, 0x34}); s = c.step();
    CHECK(s.cycles == 3 && c.machineCycles == 3 && c.instructions == 1, "long cycles");

    c.hardReset(); b.m[0] = 0x00; s = c.step();
    CHECK(c.idle && s.idle && !c.halted, "IDL waits");

    c.hardReset(); b.m[0] = 0x68; s = c.step();
    CHECK(c.halted && s.halted && c.undefinedOpcode == 0x68, "undefined opcode trap");
}

int main() {
    test_state_api();
    test_register_families();
    test_memory_and_indexing();
    test_arithmetic_logic_and_shifts();
    test_branches_and_skips();
    test_q_ef_io();
    test_control_interrupt_cycles();
    printf("CDP1802 ISA regression: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
