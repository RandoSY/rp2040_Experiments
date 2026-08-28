#include "cdp1802.h"
#include <string.h>

static inline uint16_t u16(uint32_t v) { return (uint16_t)(v & 0xffffu); }

CDP1802::CDP1802(CDP1802Bus &bus) : bus_(bus) { hardReset(); }

void CDP1802::hardReset() {
    memset(R, 0, sizeof(R));
    D = DF = T = 0;
    memset(EF, 0, sizeof(EF));
    Q = 0;
    instructions = machineCycles = 0;
    reset1802();
}

void CDP1802::reset1802() {
    // CDP1802 CLEAR/WAIT initialization: P=0, X=0, IE=1, Q=0, R0=0.
    // D, DF, T and R1-RF are deliberately not specified/reset by the chip.
    R[0] = 0;
    P = 0;
    X = 0;
    IE = 1;
    setQ(false);
    idle = false;
    halted = false;
    pendingInterrupt = false;
    undefinedOpcode = 0;
}

void CDP1802::setEF(unsigned n, bool asserted) {
    if (n >= 1 && n <= 4) EF[n - 1] = asserted;
}

CDP1802State CDP1802::snapshot() const {
    CDP1802State s{};
    memcpy(s.R, R, sizeof(R));
    s.D = D;
    s.DF = DF;
    s.P = P;
    s.X = X;
    s.T = T;
    s.IE = IE;
    s.Q = Q;
    memcpy(s.EF, EF, sizeof(EF));
    s.idle = idle;
    s.halted = halted;
    s.pendingInterrupt = pendingInterrupt;
    s.undefinedOpcode = undefinedOpcode;
    s.instructions = instructions;
    s.machineCycles = machineCycles;
    return s;
}

void CDP1802::restore(const CDP1802State &s) {
    memcpy(R, s.R, sizeof(R));
    D = s.D;
    DF = s.DF ? 1 : 0;
    P = s.P & 0x0f;
    X = s.X & 0x0f;
    T = s.T;
    IE = s.IE ? 1 : 0;
    memcpy(EF, s.EF, sizeof(EF));
    idle = s.idle;
    halted = s.halted;
    pendingInterrupt = s.pendingInterrupt;
    undefinedOpcode = s.undefinedOpcode;
    instructions = s.instructions;
    machineCycles = s.machineCycles;
    setQ(s.Q != 0);
}

void CDP1802::setQ(bool value) {
    Q = value ? 1 : 0;
    bus_.qChanged(value);
}

uint8_t CDP1802::fetch() {
    uint16_t a = pc();
    uint8_t v = bus_.read(a);
    setPC(u16((uint32_t)a + 1));
    return v;
}

uint8_t CDP1802::imm() { return fetch(); }

bool CDP1802::interrupt() {
    if (!IE) return false;
    T = (uint8_t)(((X & 0x0f) << 4) | (P & 0x0f));
    X = 2;
    P = 1;
    IE = 0;
    idle = false;
    pendingInterrupt = false;
    machineCycles += 1;
    return true;
}

void CDP1802::add(uint8_t a, uint8_t b, bool carry) {
    uint16_t s = (uint16_t)a + (uint16_t)b + (carry ? 1u : 0u);
    D = (uint8_t)s;
    DF = s > 0xff ? 1 : 0;
}

void CDP1802::sub(uint8_t minuend, uint8_t subtrahend, bool borrow) {
    int16_t r = (int16_t)minuend - (int16_t)subtrahend - (borrow ? 1 : 0);
    D = (uint8_t)r;
    DF = r >= 0 ? 1 : 0;
}

bool CDP1802::condShort(uint8_t n) const {
    switch (n & 0x0f) {
        case 0: return true;
        case 1: return Q != 0;
        case 2: return D == 0;
        case 3: return DF != 0;
        case 4: return EF[0];
        case 5: return EF[1];
        case 6: return EF[2];
        case 7: return EF[3];
        case 9: return Q == 0;
        case 10: return D != 0;
        case 11: return DF == 0;
        case 12: return !EF[0];
        case 13: return !EF[1];
        case 14: return !EF[2];
        case 15: return !EF[3];
        default: return false;
    }
}

bool CDP1802::condLong(uint8_t n) const {
    switch (n & 0x0f) {
        case 0: return true;
        case 1: return Q != 0;
        case 2: return D == 0;
        case 3: return DF != 0;
        case 9: return Q == 0;
        case 10: return D != 0;
        case 11: return DF == 0;
        default: return false;
    }
}

bool CDP1802::condLongSkip(uint8_t n) const {
    switch (n & 0x0f) {
        case 5: return Q == 0;       // LSNQ
        case 6: return D != 0;       // LSNZ
        case 7: return DF == 0;      // LSNF
        case 8: return true;         // LSKP
        case 12: return IE != 0;     // LSIE
        case 13: return Q != 0;      // LSQ
        case 14: return D == 0;      // LSZ
        case 15: return DF != 0;     // LSDF
        default: return false;
    }
}

void CDP1802::shortBranch(bool condition) {
    uint16_t a = pc();
    uint8_t target = bus_.read(a);
    if (condition) setPC((uint16_t)((a & 0xff00u) | target));
    else setPC(u16((uint32_t)a + 1));
}

void CDP1802::longBranch(bool condition) {
    uint16_t a = pc();
    uint8_t hi = bus_.read(a);
    uint8_t lo = bus_.read(u16((uint32_t)a + 1));
    if (condition) setPC((uint16_t)(((uint16_t)hi << 8) | lo));
    else setPC(u16((uint32_t)a + 2));
}

void CDP1802::longSkip(bool condition) {
    if (condition) setPC(u16((uint32_t)pc() + 2));
}

CDP1802Step CDP1802::step() {
    CDP1802Step result{};
    if (halted) { result.halted = true; return result; }
    if (pendingInterrupt && IE) {
        result.pc = pc();
        result.interrupt = interrupt();
        result.cycles = result.interrupt ? 1 : 0;
        return result;
    }
    if (idle) { result.idle = true; return result; }

    result.pc = pc();
    const uint8_t op = fetch();
    result.opcode = op;
    const uint8_t I = op >> 4;
    const uint8_t N = op & 0x0f;
    uint8_t cycles = (I == 0x0c) ? 3 : 2;

    switch (I) {
        case 0x0:
            if (N == 0) idle = true; // IDL waits for interrupt/DMA, it is not a halt.
            else D = bus_.read(R[N]);
            break;
        case 0x1: R[N] = u16((uint32_t)R[N] + 1); break;
        case 0x2: R[N] = u16((uint32_t)R[N] - 1); break;
        case 0x3:
            if (N == 8) setPC(u16((uint32_t)pc() + 1)); // SKP
            else shortBranch(condShort(N));
            break;
        case 0x4: D = bus_.read(R[N]); R[N] = u16((uint32_t)R[N] + 1); break;
        case 0x5: bus_.write(R[N], D); break;
        case 0x6:
            if (N == 0) R[X] = u16((uint32_t)R[X] + 1); // IRX
            else if (N <= 7) {
                uint16_t a = R[X];
                uint8_t v = bus_.read(a);
                bus_.output(N, v);
                R[X] = u16((uint32_t)a + 1);
            } else if (N == 8) {
                undefinedOpcode = op;
                if (trapUndefined) halted = true;
            } else {
                uint8_t port = (uint8_t)(N - 8);
                uint8_t v = bus_.input(port);
                bus_.write(R[X], v);
                D = v;
            }
            break;
        case 0x7:
            switch (N) {
                case 0: {
                    uint16_t a = R[X]; uint8_t v = bus_.read(a); R[X] = u16((uint32_t)a + 1);
                    X = v >> 4; P = v & 0x0f; IE = 1; break;
                }
                case 1: {
                    uint16_t a = R[X]; uint8_t v = bus_.read(a); R[X] = u16((uint32_t)a + 1);
                    X = v >> 4; P = v & 0x0f; IE = 0; break;
                }
                case 2: { uint16_t a = R[X]; D = bus_.read(a); R[X] = u16((uint32_t)a + 1); break; }
                case 3: { uint16_t a = R[X]; bus_.write(a, D); R[X] = u16((uint32_t)a - 1); break; }
                case 4: add(D, bus_.read(R[X]), DF != 0); break;
                case 5: sub(bus_.read(R[X]), D, DF == 0); break;
                case 6: { uint8_t old = DF; DF = D & 1; D = (uint8_t)((D >> 1) | (old ? 0x80 : 0)); break; }
                case 7: sub(D, bus_.read(R[X]), DF == 0); break;
                case 8: bus_.write(R[X], T); break;
                case 9:
                    T = (uint8_t)(((X & 0x0f) << 4) | (P & 0x0f));
                    bus_.write(R[2], T); X = P; R[2] = u16((uint32_t)R[2] - 1); break;
                case 10: setQ(false); break;
                case 11: setQ(true); break;
                case 12: add(D, imm(), DF != 0); break;
                case 13: { uint8_t v = imm(); sub(v, D, DF == 0); break; }
                case 14: { uint8_t old = DF; DF = (D >> 7) & 1; D = (uint8_t)((D << 1) | (old ? 1 : 0)); break; }
                case 15: { uint8_t v = imm(); sub(D, v, DF == 0); break; }
            }
            break;
        case 0x8: D = (uint8_t)(R[N] & 0xff); break;
        case 0x9: D = (uint8_t)(R[N] >> 8); break;
        case 0xA: R[N] = (uint16_t)((R[N] & 0xff00u) | D); break;
        case 0xB: R[N] = (uint16_t)((R[N] & 0x00ffu) | ((uint16_t)D << 8)); break;
        case 0xC:
            if (N <= 3 || (N >= 9 && N <= 11)) longBranch(condLong(N));
            else if (N == 4) { }
            else longSkip(condLongSkip(N));
            break;
        case 0xD: P = N; break;
        case 0xE: X = N; break;
        case 0xF:
            switch (N) {
                case 0: D = bus_.read(R[X]); break;
                case 1: D = (uint8_t)(D | bus_.read(R[X])); break;
                case 2: D = (uint8_t)(D & bus_.read(R[X])); break;
                case 3: D = (uint8_t)(D ^ bus_.read(R[X])); break;
                case 4: add(D, bus_.read(R[X]), false); break;
                case 5: sub(bus_.read(R[X]), D, false); break;
                case 6: DF = D & 1; D >>= 1; break;
                case 7: sub(D, bus_.read(R[X]), false); break;
                case 8: D = imm(); break;
                case 9: D = (uint8_t)(D | imm()); break;
                case 10: D = (uint8_t)(D & imm()); break;
                case 11: D = (uint8_t)(D ^ imm()); break;
                case 12: add(D, imm(), false); break;
                case 13: { uint8_t v = imm(); sub(v, D, false); break; }
                case 14: DF = (D >> 7) & 1; D = (uint8_t)(D << 1); break;
                case 15: { uint8_t v = imm(); sub(D, v, false); break; }
            }
            break;
    }

    machineCycles += cycles;
    instructions++;
    result.cycles = cycles;
    result.idle = idle;
    result.halted = halted;
    return result;
}
