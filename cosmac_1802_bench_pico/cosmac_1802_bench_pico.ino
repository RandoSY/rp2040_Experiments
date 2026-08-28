#include <Arduino.h>
#include "cdp1802.h"

// COSMAC 1802 BENCH firmware for Raspberry Pi Pico / RP2040.
// Purpose: a clean, stand-alone 1802 laboratory target.
// No Galileo flight image, Forth, magnetometer, storage image, or periodic IRQ logic.

namespace {

class BenchBus : public CDP1802Bus {
public:
  uint8_t memory[65536]{};
  uint8_t inputLatch[8]{};
  uint8_t outputLatch[8]{};

  uint8_t read(uint16_t address) override { return memory[address]; }
  void write(uint16_t address, uint8_t value) override { memory[address] = value; }

  uint8_t input(uint8_t port) override { return inputLatch[port & 7u]; }
  void output(uint8_t port, uint8_t value) override { outputLatch[port & 7u] = value; }

  void qChanged(bool q) override {
    qLevel_ = q;
    if (qIndicatorReady_) digitalWrite(LED_BUILTIN, q ? HIGH : LOW);
  }

  void beginQIndicator() {
    pinMode(LED_BUILTIN, OUTPUT);
    qIndicatorReady_ = true;
    digitalWrite(LED_BUILTIN, qLevel_ ? HIGH : LOW);
  }

private:
  bool qIndicatorReady_ = false;
  bool qLevel_ = false;
};

BenchBus bus;
CDP1802 cpu(bus);

bool running = false;
uint32_t cpuHz = 1600000;
uint32_t lastTickUs = 0;
int64_t clockBudget = 0;

char lineBuffer[320];
size_t lineLength = 0;

constexpr int64_t CLOCK_UNIT = 1000000LL;
constexpr int64_t MIN_STEP_COST = 16LL * CLOCK_UNIT;

char upperChar(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

bool ieq(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (upperChar(*a++) != upperChar(*b++)) return false;
  }
  return *a == 0 && *b == 0;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = upperChar(c);
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHex(const char *s, uint32_t maxValue, uint32_t &value) {
  if (!s || !*s) return false;
  if (*s == '$') ++s;
  else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  if (!*s) return false;
  char *end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  if (!end || *end || v > maxValue) return false;
  value = (uint32_t)v;
  return true;
}

bool parseUnsigned(const char *s, uint32_t minValue, uint32_t maxValue, uint32_t &value) {
  if (!s || !*s) return false;
  char *end = nullptr;
  unsigned long v = strtoul(s, &end, 0);
  if (!end || *end || v < minValue || v > maxValue) return false;
  value = (uint32_t)v;
  return true;
}

void hex2(Stream &io, uint8_t v) {
  const char d[] = "0123456789ABCDEF";
  io.print(d[(v >> 4) & 0x0f]);
  io.print(d[v & 0x0f]);
}

void hex4(Stream &io, uint16_t v) {
  hex2(io, (uint8_t)(v >> 8));
  hex2(io, (uint8_t)v);
}

void hex8(Stream &io, uint32_t v) {
  hex4(io, (uint16_t)(v >> 16));
  hex4(io, (uint16_t)v);
}

void hex16(Stream &io, uint64_t v) {
  hex8(io, (uint32_t)(v >> 32));
  hex8(io, (uint32_t)v);
}

void printState(Stream &io) {
  io.print("@1802 STATE ");
  for (unsigned i = 0; i < 16; ++i) {
    io.print('R'); io.print("0123456789ABCDEF"[i]); io.print('=');
    hex4(io, cpu.R[i]); io.print(' ');
  }
  io.print("D="); hex2(io, cpu.D);
  io.print(" DF="); io.print(cpu.DF ? '1' : '0');
  io.print(" P="); io.print("0123456789ABCDEF"[cpu.P & 0x0f]);
  io.print(" X="); io.print("0123456789ABCDEF"[cpu.X & 0x0f]);
  io.print(" T="); hex2(io, cpu.T);
  io.print(" IE="); io.print(cpu.IE ? '1' : '0');
  io.print(" Q="); io.print(cpu.Q ? '1' : '0');
  io.print(" EF=");
  for (unsigned i = 0; i < 4; ++i) io.print(cpu.EF[i] ? '1' : '0');
  io.print(" PC="); hex4(io, cpu.pc());
  io.print(" IDL="); io.print(cpu.idle ? '1' : '0');
  io.print(" HALT="); io.print(cpu.halted ? '1' : '0');
  io.print(" RUN="); io.print(running ? '1' : '0');
  io.print(" INS="); hex16(io, cpu.instructions);
  io.print(" CYC="); hex16(io, cpu.machineCycles);
  io.print(" HZ="); io.println(cpuHz);
}

void printHelp(Stream &io) {
  io.println("COSMAC 1802 BENCH - stand-alone RP2040 target");
  io.println("@RUN                 run from current PC");
  io.println("@STOP                stop execution");
  io.println("@STEP                execute one instruction");
  io.println("@RESET               reset CPU only; RAM is preserved");
  io.println("@STATE               show CPU state");
  io.println("@SETPC hex16         set current program counter");
  io.println("@POKE hex16 bytes... load RAM (max 40 hex bytes per line)");
  io.println("@PEEK hex16 [count]  read RAM (count hex, max 40)");
  io.println("@SET R0..RF|D|DF|P|X|T|IE|Q value");
  io.println("@EF 1..4 0|1         set EF input");
  io.println("@SPEED hz            set 1802 input clock, 1000..32000000");
  io.println("Legacy aliases: .RUN = @RUN, .PAUSE = @STOP");
}

void stopExecution() { running = false; clockBudget = 0; }
void startExecution() { running = true; lastTickUs = micros(); clockBudget = 0; }

bool setNamedState(const char *name, uint32_t value) {
  CDP1802State s = cpu.snapshot();
  if (name && (name[0] == 'R' || name[0] == 'r') && name[1] && !name[2]) {
    int n = hexNibble(name[1]);
    if (n < 0 || value > 0xffffu) return false;
    s.R[n] = (uint16_t)value;
  } else if (ieq(name, "D")) {
    if (value > 0xffu) return false;
    s.D = (uint8_t)value;
  } else if (ieq(name, "DF")) {
    if (value > 1u) return false;
    s.DF = (uint8_t)value;
  } else if (ieq(name, "P")) {
    if (value > 0x0fu) return false;
    s.P = (uint8_t)value;
  } else if (ieq(name, "X")) {
    if (value > 0x0fu) return false;
    s.X = (uint8_t)value;
  } else if (ieq(name, "T")) {
    if (value > 0xffu) return false;
    s.T = (uint8_t)value;
  } else if (ieq(name, "IE")) {
    if (value > 1u) return false;
    s.IE = (uint8_t)value;
  } else if (ieq(name, "Q")) {
    if (value > 1u) return false;
    s.Q = (uint8_t)value;
  } else {
    return false;
  }
  cpu.restore(s);
  return true;
}

void error(Stream &io, const char *message) {
  io.print("@1802 ERR "); io.println(message);
}

void executeCommand(char *line) {
  if (!line || !*line) return;

  if (ieq(line, "RUN") || ieq(line, ".RUN")) strcpy(line, "@RUN");
  else if (ieq(line, "STOP") || ieq(line, ".PAUSE")) strcpy(line, "@STOP");
  else if (ieq(line, "STEP") || ieq(line, ".STEP")) strcpy(line, "@STEP");
  else if (ieq(line, "RESET") || ieq(line, ".RESET")) strcpy(line, "@RESET");
  else if (ieq(line, "STATE") || ieq(line, ".REGS")) strcpy(line, "@STATE");
  else if (ieq(line, "HELP")) strcpy(line, "@HELP");

  char *cmd = strtok(line, " \t");
  if (!cmd) return;

  if (ieq(cmd, "@HELP")) { printHelp(Serial); return; }
  if (ieq(cmd, "@STATE")) { printState(Serial); return; }

  if (ieq(cmd, "@RUN")) {
    startExecution();
    Serial.println("@1802 OK RUN");
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@STOP")) {
    stopExecution();
    Serial.println("@1802 OK STOP");
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@RESET")) {
    stopExecution();
    cpu.hardReset();
    Serial.println("@1802 OK RESET");
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@STEP")) {
    stopExecution();
    CDP1802Step r = cpu.step();
    Serial.print("@1802 STEP PC="); hex4(Serial, r.pc);
    Serial.print(" OP="); hex2(Serial, r.opcode);
    Serial.print(" MC="); hex2(Serial, r.cycles);
    Serial.print(" IDL="); Serial.print(r.idle ? '1' : '0');
    Serial.print(" HALT="); Serial.println(r.halted ? '1' : '0');
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@SETPC")) {
    stopExecution();
    char *a = strtok(nullptr, " \t");
    uint32_t v = 0;
    if (!parseHex(a, 0xffffu, v)) { error(Serial, "SETPC expects hex16"); return; }
    cpu.setPC((uint16_t)v);
    Serial.print("@1802 OK SETPC="); hex4(Serial, (uint16_t)v); Serial.println();
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@POKE")) {
    stopExecution();
    char *as = strtok(nullptr, " \t");
    uint32_t address = 0;
    if (!parseHex(as, 0xffffu, address)) { error(Serial, "POKE expects hex16 byte_hex ..."); return; }
    uint8_t bytes[0x40]{};
    unsigned count = 0;
    for (char *bs = strtok(nullptr, " \t"); bs; bs = strtok(nullptr, " \t")) {
      if (count >= sizeof(bytes)) { error(Serial, "POKE maximum is 40 hex bytes"); return; }
      uint32_t b = 0;
      if (!parseHex(bs, 0xffu, b)) { error(Serial, "POKE contains invalid byte; no bytes written"); return; }
      bytes[count++] = (uint8_t)b;
    }
    if (!count) { error(Serial, "POKE requires at least one byte"); return; }
    for (unsigned i = 0; i < count; ++i) bus.write((uint16_t)(address + i), bytes[i]);
    Serial.print("@1802 OK POKE ADDR="); hex4(Serial, (uint16_t)address);
    Serial.print(" COUNT="); hex2(Serial, (uint8_t)count); Serial.println();
    return;
  }
  if (ieq(cmd, "@PEEK")) {
    char *as = strtok(nullptr, " \t");
    char *cs = strtok(nullptr, " \t");
    uint32_t address = 0, count = 1;
    if (!parseHex(as, 0xffffu, address) || (cs && !parseHex(cs, 0x40u, count)) || count == 0 || count > 0x40u) {
      error(Serial, "PEEK expects hex16 [count_hex 01..40]"); return;
    }
    Serial.print("@1802 MEM ADDR="); hex4(Serial, (uint16_t)address); Serial.print(" DATA=");
    for (uint32_t i = 0; i < count; ++i) hex2(Serial, bus.read((uint16_t)(address + i)));
    Serial.println();
    return;
  }
  if (ieq(cmd, "@SET")) {
    stopExecution();
    char *name = strtok(nullptr, " \t");
    char *vs = strtok(nullptr, " \t");
    uint32_t v = 0;
    if (!name || !parseHex(vs, 0xffffu, v) || !setNamedState(name, v)) {
      error(Serial, "SET expects R0..RF|D|DF|P|X|T|IE|Q and valid hex value"); return;
    }
    Serial.print("@1802 OK SET "); Serial.print(name); Serial.print('='); Serial.println(vs);
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@EF")) {
    stopExecution();
    char *ns = strtok(nullptr, " \t");
    char *vs = strtok(nullptr, " \t");
    if (!ns || !vs || ns[1] || vs[1] || ns[0] < '1' || ns[0] > '4' || (vs[0] != '0' && vs[0] != '1')) {
      error(Serial, "EF expects: @EF 1..4 0|1"); return;
    }
    unsigned n = (unsigned)(ns[0] - '0');
    cpu.setEF(n, vs[0] == '1');
    Serial.print("@1802 OK EF"); Serial.print(n); Serial.print('='); Serial.println(vs[0]);
    printState(Serial);
    return;
  }
  if (ieq(cmd, "@SPEED")) {
    stopExecution();
    char *hs = strtok(nullptr, " \t");
    uint32_t hz = 0;
    if (!parseUnsigned(hs, 1000u, 32000000u, hz)) {
      error(Serial, "SPEED expects decimal Hz 1000..32000000"); return;
    }
    cpuHz = hz;
    Serial.print("@1802 OK SPEED="); Serial.println(cpuHz);
    return;
  }

  error(Serial, "unknown command; use @HELP");
}

void serviceSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuffer[lineLength] = 0;
      executeCommand(lineBuffer);
      lineLength = 0;
      continue;
    }
    if (lineLength + 1 < sizeof(lineBuffer)) lineBuffer[lineLength++] = c;
    else lineLength = 0;
  }
}

void runCpu() {
  if (!running || cpu.halted || cpu.idle) return;

  uint32_t now = micros();
  uint32_t elapsed = now - lastTickUs;
  lastTickUs = now;
  if (elapsed > 50000u) elapsed = 50000u;
  clockBudget += (int64_t)elapsed * (int64_t)cpuHz;

  unsigned guard = 0;
  while (running && !cpu.halted && !cpu.idle && clockBudget >= MIN_STEP_COST && guard < 5000u) {
    CDP1802Step r = cpu.step();
    if (!r.cycles) break;
    clockBudget -= (int64_t)r.cycles * 8LL * CLOCK_UNIT;
    ++guard;
  }

  if (cpu.halted || cpu.idle) {
    running = false;
    Serial.println(cpu.halted ? "@1802 STOP HALT" : "@1802 STOP IDL");
    printState(Serial);
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  bus.beginQIndicator();
  cpu.hardReset();
  stopExecution();
  delay(50);
  Serial.println();
  Serial.println("COSMAC 1802 BENCH / RP2040");
  Serial.println("64K RAM - Q on Pico LED - no flight-control software");
  Serial.println("Type @HELP for commands.");
  printState(Serial);
}

void loop() {
  serviceSerial();
  runCpu();
}
