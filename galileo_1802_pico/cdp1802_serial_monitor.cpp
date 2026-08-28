#include "cdp1802_serial_monitor.h"

#include <stdlib.h>
#include <string.h>

namespace {

static bool ieq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y) return false;
    }
    return *a == 0 && *b == 0;
}

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parseHex(const char *s, uint32_t maxValue, uint32_t &value) {
    if (!s || !*s) return false;
    if (*s == '$') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return false;
    char *end = nullptr;
    unsigned long v = strtoul(s, &end, 16);
    if (!end || *end || v > maxValue) return false;
    value = (uint32_t)v;
    return true;
}

static void hex2(Stream &io, uint8_t v) {
    const char digits[] = "0123456789ABCDEF";
    io.print(digits[(v >> 4) & 0x0f]);
    io.print(digits[v & 0x0f]);
}

static void hex4(Stream &io, uint16_t v) {
    hex2(io, (uint8_t)(v >> 8));
    hex2(io, (uint8_t)v);
}

static void hex8(Stream &io, uint32_t v) {
    hex4(io, (uint16_t)(v >> 16));
    hex4(io, (uint16_t)v);
}

static void hex16(Stream &io, uint64_t v) {
    hex8(io, (uint32_t)(v >> 32));
    hex8(io, (uint32_t)v);
}

static void printState(Stream &io, const CDP1802 &cpu) {
    io.print("@1802 STATE ");
    for (unsigned i = 0; i < 16; i++) {
        io.print('R');
        io.print("0123456789ABCDEF"[i]);
        io.print('=');
        hex4(io, cpu.R[i]);
        io.print(' ');
    }
    io.print("D="); hex2(io, cpu.D);
    io.print(" DF="); io.print(cpu.DF ? '1' : '0');
    io.print(" P="); io.print("0123456789ABCDEF"[cpu.P & 0x0f]);
    io.print(" X="); io.print("0123456789ABCDEF"[cpu.X & 0x0f]);
    io.print(" T="); hex2(io, cpu.T);
    io.print(" IE="); io.print(cpu.IE ? '1' : '0');
    io.print(" Q="); io.print(cpu.Q ? '1' : '0');
    io.print(" EF=");
    for (unsigned i = 0; i < 4; i++) io.print(cpu.EF[i] ? '1' : '0');
    io.print(" PC="); hex4(io, cpu.pc());
    io.print(" IDL="); io.print(cpu.idle ? '1' : '0');
    io.print(" HALT="); io.print(cpu.halted ? '1' : '0');
    io.print(" PIRQ="); io.print(cpu.pendingInterrupt ? '1' : '0');
    io.print(" UOP="); hex2(io, cpu.undefinedOpcode);
    io.print(" INS="); hex16(io, cpu.instructions);
    io.print(" CYC="); hex16(io, cpu.machineCycles);
    io.println();
}

static void error(Stream &io, const char *message) {
    io.print("@1802 ERR ");
    io.println(message);
}

static bool setNamedState(CDP1802 &cpu, const char *name, uint32_t value) {
    CDP1802State s = cpu.snapshot();

    if (name && (name[0] == 'R' || name[0] == 'r') && name[1] && !name[2]) {
        int n = hexNibble(name[1]);
        if (n < 0 || value > 0xffffu) return false;
        s.R[n] = (uint16_t)value;
    } else if (ieq(name, "D")) {
        if (value > 0xffu) return false; s.D = (uint8_t)value;
    } else if (ieq(name, "DF")) {
        if (value > 1u) return false; s.DF = (uint8_t)value;
    } else if (ieq(name, "P")) {
        if (value > 0x0fu) return false; s.P = (uint8_t)value;
    } else if (ieq(name, "X")) {
        if (value > 0x0fu) return false; s.X = (uint8_t)value;
    } else if (ieq(name, "T")) {
        if (value > 0xffu) return false; s.T = (uint8_t)value;
    } else if (ieq(name, "IE")) {
        if (value > 1u) return false; s.IE = (uint8_t)value;
    } else if (ieq(name, "Q")) {
        if (value > 1u) return false; s.Q = (uint8_t)value;
    } else if (ieq(name, "IDL")) {
        if (value > 1u) return false; s.idle = value != 0;
    } else if (ieq(name, "HALT")) {
        if (value > 1u) return false; s.halted = value != 0;
    } else {
        return false;
    }

    cpu.restore(s);
    return true;
}

} // namespace

bool handleCdp1802MonitorCommand(Stream &io, CDP1802 &cpu, CDP1802Bus &bus, char *line) {
    if (!line || line[0] != '@') return false;

    char *cmd = strtok(line, " \t");
    if (!cmd) return true;

    if (ieq(cmd, "@HELP")) {
        io.println("@1802 HELP PROTO=1");
        io.println("@STATE");
        io.println("@STEP");
        io.println("@RESET");
        io.println("@IRQ");
        io.println("@SETPC hex16");
        io.println("@SET R0..RF|D|DF|P|X|T|IE|Q|IDL|HALT hex");
        io.println("@EF 1..4 0|1");
        io.println("@PEEK hex16 [count_hex, max 40]");
        io.println("@POKE hex16 byte_hex ... [max 40 bytes]");
        return true;
    }

    if (ieq(cmd, "@STATE")) {
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@STEP")) {
        CDP1802Step r = cpu.step();
        io.print("@1802 STEP PC="); hex4(io, r.pc);
        io.print(" OP="); hex2(io, r.opcode);
        io.print(" MC="); hex2(io, r.cycles);
        io.print(" IRQ="); io.print(r.interrupt ? '1' : '0');
        io.print(" IDL="); io.print(r.idle ? '1' : '0');
        io.print(" HALT="); io.println(r.halted ? '1' : '0');
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@RESET")) {
        cpu.hardReset();
        io.println("@1802 OK RESET");
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@IRQ")) {
        bool accepted = cpu.interrupt();
        io.print("@1802 IRQ ACCEPTED=");
        io.println(accepted ? '1' : '0');
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@SETPC")) {
        char *a = strtok(nullptr, " \t");
        uint32_t v = 0;
        if (!parseHex(a, 0xffffu, v)) { error(io, "SETPC expects hex16"); return true; }
        cpu.setPC((uint16_t)v);
        io.print("@1802 OK SETPC="); hex4(io, (uint16_t)v); io.println();
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@SET")) {
        char *name = strtok(nullptr, " \t");
        char *val = strtok(nullptr, " \t");
        uint32_t v = 0;
        if (!name || !parseHex(val, 0xffffu, v) || !setNamedState(cpu, name, v)) {
            error(io, "SET expects R0..RF|D|DF|P|X|T|IE|Q|IDL|HALT and valid hex value");
            return true;
        }
        io.print("@1802 OK SET "); io.print(name); io.print('='); io.println(val);
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@EF")) {
        char *ns = strtok(nullptr, " \t");
        char *vs = strtok(nullptr, " \t");
        if (!ns || !vs || ns[1] || vs[1] || ns[0] < '1' || ns[0] > '4' || (vs[0] != '0' && vs[0] != '1')) {
            error(io, "EF expects: @EF 1..4 0|1");
            return true;
        }
        unsigned n = (unsigned)(ns[0] - '0');
        cpu.setEF(n, vs[0] == '1');
        io.print("@1802 OK EF"); io.print(n); io.print('='); io.println(vs[0]);
        printState(io, cpu);
        return true;
    }

    if (ieq(cmd, "@PEEK")) {
        char *as = strtok(nullptr, " \t");
        char *cs = strtok(nullptr, " \t");
        uint32_t a = 0, count = 1;
        if (!parseHex(as, 0xffffu, a) || (cs && !parseHex(cs, 0x40u, count)) || count == 0 || count > 0x40u) {
            error(io, "PEEK expects hex16 [count_hex 01..40]");
            return true;
        }
        io.print("@1802 MEM ADDR="); hex4(io, (uint16_t)a); io.print(" DATA=");
        for (uint32_t i = 0; i < count; i++) hex2(io, bus.read((uint16_t)(a + i)));
        io.println();
        return true;
    }

    if (ieq(cmd, "@POKE")) {
        char *as = strtok(nullptr, " \t");
        uint32_t a = 0;
        if (!parseHex(as, 0xffffu, a)) { error(io, "POKE expects hex16 byte_hex ..."); return true; }
        unsigned count = 0;
        for (char *bs = strtok(nullptr, " \t"); bs; bs = strtok(nullptr, " \t")) {
            if (count >= 0x40u) { error(io, "POKE maximum is 40 hex bytes"); return true; }
            uint32_t b = 0;
            if (!parseHex(bs, 0xffu, b)) { error(io, "POKE contains invalid byte"); return true; }
            bus.write((uint16_t)(a + count), (uint8_t)b);
            count++;
        }
        if (!count) { error(io, "POKE requires at least one byte"); return true; }
        io.print("@1802 OK POKE ADDR="); hex4(io, (uint16_t)a); io.print(" COUNT="); hex2(io, (uint8_t)count); io.println();
        return true;
    }

    error(io, "unknown @ command; use @HELP");
    return true;
}
