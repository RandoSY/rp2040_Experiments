#include <Arduino.h>
#include <strings.h>
#include "cdp1802.h"
#include "native_rom_v8_segments.h"
namespace {
constexpr uint8_t GS = 0x1D;
constexpr uint16_t RX_CAP = 256;
class BenchBus : public CDP1802Bus {
public:
  uint8_t memory[65536]{};
  uint8_t outputLatch[8]{};
  uint16_t adc[2]{512, 256};
  uint8_t switches = 0;
  uint8_t adcSelect = 0;
  void attach(CDP1802 *cpu) { cpu_ = cpu; updateEf4(); }
  uint8_t read(uint16_t address) override { return memory[address]; }
  void write(uint16_t address, uint8_t value) override { if (address >= MF_ROM_START && address < MF_ROM_END) return; memory[address] = value; }
  uint8_t input(uint8_t port) override {
    port &= 7u;
    if (port == 7) return popRx();
    if (port == 1) return switches;
    const uint16_t v = adc[adcSelect & 1u] & 0x03ffu;
    if (port == 2) return (uint8_t)v;
    if (port == 3) return (uint8_t)(v >> 8);
    return 0;
  }
  void output(uint8_t port, uint8_t value) override {
    port &= 7u; outputLatch[port] = value;
    if (port == 2) adcSelect = value & 1u;
    if (port == 7) guestWrite(value);
  }
  void qChanged(bool q) override { qLevel_ = q; if (qReady_) digitalWrite(LED_BUILTIN, q ? HIGH : LOW); }
  void beginQ() { pinMode(LED_BUILTIN, OUTPUT); qReady_ = true; digitalWrite(LED_BUILTIN, qLevel_ ? HIGH : LOW); }
  void clearAndInstallRom() {
    memset(memory, 0, sizeof(memory));
    for (uint16_t s = 0; s < MF_ROM_SEGMENT_COUNT; ++s) {
      const MfRomSegment &seg = MF_ROM_SEGMENTS[s];
      for (uint16_t i = 0; i < seg.length; ++i) {
        const int h = hexNibble(seg.hex[i * 2]); const int l = hexNibble(seg.hex[i * 2 + 1]);
        memory[(uint16_t)(seg.address + i)] = (uint8_t)((h << 4) | l);
      }
    }
    clearRx(); memset(outputLatch, 0, sizeof(outputLatch)); adcSelect = 0;
  }
  void enqueue(uint8_t value) {
    const uint16_t next = (uint16_t)((rxHead_ + 1u) % RX_CAP);
    if (next == rxTail_) return;
    rx_[rxHead_] = value; rxHead_ = next; updateEf4();
  }
  void clearRx() { rxHead_ = rxTail_ = 0; updateEf4(); }
private:
  CDP1802 *cpu_ = nullptr;
  uint8_t rx_[RX_CAP]{};
  uint16_t rxHead_ = 0, rxTail_ = 0;
  bool qReady_ = false, qLevel_ = false;
  static int hexNibble(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return 0; }
  uint8_t popRx() { if (rxHead_ == rxTail_) { updateEf4(); return 0; } const uint8_t v = rx_[rxTail_]; rxTail_ = (uint16_t)((rxTail_ + 1u) % RX_CAP); updateEf4(); return v; }
  void updateEf4() { if (cpu_) cpu_->setEF(4, rxHead_ != rxTail_); }
  void guestWrite(uint8_t value) { if (value == GS) Serial.write(GS); Serial.write(value); }
};
BenchBus bus;
CDP1802 cpu(bus);
bool running = false;
bool forthMode = true;
uint32_t cpuHz = 1600000;
uint32_t lastTickUs = 0;
int64_t clockBudget = 0;
constexpr int64_t CLOCK_UNIT = 1000000LL;
constexpr int64_t MIN_INSTR_COST = 16LL * CLOCK_UNIT;
void startRun() { running = true; lastTickUs = micros(); clockBudget = 0; }
void stopRun() { running = false; clockBudget = 0; }
void monitorPrefix(char kind = '=') { Serial.write(GS); Serial.write((uint8_t)kind); }
void monitorLine(const char *s) { monitorPrefix('='); Serial.println(s); }
void monitorError(const char *s) { monitorPrefix('?'); Serial.println(s); }
void hex2(Stream &io, uint8_t v) { static const char d[] = "0123456789ABCDEF"; io.print(d[(v >> 4) & 15]); io.print(d[v & 15]); }
void hex4(Stream &io, uint16_t v) { hex2(io, (uint8_t)(v >> 8)); hex2(io, (uint8_t)v); }
void hex16(Stream &io, uint64_t v) { hex4(io, (uint16_t)(v >> 48)); hex4(io, (uint16_t)(v >> 32)); hex4(io, (uint16_t)(v >> 16)); hex4(io, (uint16_t)v); }
bool parseHex(const char *s, uint32_t maxValue, uint32_t &out) {
  if (!s || !*s) return false; if (*s == '$') ++s; else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2; if (!*s) return false;
  char *end = nullptr; unsigned long v = strtoul(s, &end, 16); if (!end || *end || v > maxValue) return false; out = (uint32_t)v; return true;
}
bool parseDec(const char *s, uint32_t lo, uint32_t hi, uint32_t &out) {
  if (!s || !*s) return false; char *end = nullptr; unsigned long v = strtoul(s, &end, 10); if (!end || *end || v < lo || v > hi) return false; out = (uint32_t)v; return true;
}
void sendState() {
  monitorPrefix('='); Serial.print("STATE ");
  for (unsigned i = 0; i < 16; ++i) { Serial.print('R'); Serial.print("0123456789ABCDEF"[i]); Serial.print('='); hex4(Serial, cpu.R[i]); Serial.print(' '); }
  Serial.print("D="); hex2(Serial, cpu.D); Serial.print(" DF="); Serial.print(cpu.DF ? 1 : 0); Serial.print(" P="); Serial.print(cpu.P, HEX); Serial.print(" X="); Serial.print(cpu.X, HEX);
  Serial.print(" T="); hex2(Serial, cpu.T); Serial.print(" IE="); Serial.print(cpu.IE ? 1 : 0); Serial.print(" Q="); Serial.print(cpu.Q ? 1 : 0); Serial.print(" PC="); hex4(Serial, cpu.pc());
  Serial.print(" IDL="); Serial.print(cpu.idle ? 1 : 0); Serial.print(" HALT="); Serial.print(cpu.halted ? 1 : 0); Serial.print(" RUN="); Serial.print(running ? 1 : 0); Serial.print(" MODE="); Serial.print(forthMode ? "FORTH" : "MACHINE"); Serial.print(" INS="); hex16(Serial, cpu.instructions); Serial.print(" HZ="); Serial.println(cpuHz);
}
void sendIo() {
  monitorPrefix('='); Serial.print("IO SW="); hex2(Serial, bus.switches); Serial.print(" ADC0="); Serial.print(bus.adc[0] & 0x3ff); Serial.print(" ADC1="); Serial.print(bus.adc[1] & 0x3ff); Serial.print(" SEL="); Serial.print(bus.adcSelect); Serial.print(" OUT1="); hex2(Serial, bus.outputLatch[1]); Serial.print(" OUT2="); hex2(Serial, bus.outputLatch[2]); Serial.println();
}
void coldForth() { stopRun(); bus.clearAndInstallRom(); cpu.hardReset(); cpu.setPC(MF_COLD); forthMode = true; startRun(); }
void coldMachine() { stopRun(); bus.clearAndInstallRom(); cpu.hardReset(); cpu.setPC(0x0000); forthMode = false; }
void tickCpu() {
  if (!running || cpu.halted) return;
  const uint32_t now = micros(); uint32_t elapsed = now - lastTickUs; lastTickUs = now; if (elapsed > 50000u) elapsed = 50000u;
  clockBudget += (int64_t)elapsed * (int64_t)cpuHz; unsigned guard = 0;
  while (clockBudget >= MIN_INSTR_COST && running && !cpu.halted && guard < 12000u) { if (cpu.idle) break; CDP1802Step r = cpu.step(); if (!r.cycles) break; clockBudget -= (int64_t)r.cycles * 8LL * CLOCK_UNIT; ++guard; }
}
char monitorBuf[320];
size_t monitorLen = 0;
enum class RxMode : uint8_t { Guest, Escape, Monitor };
RxMode rxMode = RxMode::Guest;
void handleMonitor(char *line) {
  while (*line == ' ' || *line == '\t') ++line; if (!*line) return; char *cmd = strtok(line, " \t"); if (!cmd) return;
  if (!strcasecmp(cmd, "BOOTF")) { coldForth(); monitorLine("BOOTF"); return; }
  if (!strcasecmp(cmd, "BOOTM")) { coldMachine(); monitorLine("BOOTM"); return; }
  if (!strcasecmp(cmd, "STOP")) { stopRun(); monitorLine("STOP"); sendState(); return; }
  if (!strcasecmp(cmd, "RUN")) { startRun(); monitorLine("RUN"); return; }
  if (!strcasecmp(cmd, "STATE")) { sendState(); return; }
  if (!strcasecmp(cmd, "IO?")) { sendIo(); return; }
  if (!strcasecmp(cmd, "RESET")) { stopRun(); cpu.hardReset(); cpu.setPC(forthMode ? MF_COLD : 0x0000); if (forthMode) startRun(); monitorLine("RESET"); return; }
  if (!strcasecmp(cmd, "STEP")) { stopRun(); CDP1802Step r = cpu.step(); monitorPrefix('='); Serial.print("STEP PC="); hex4(Serial, r.pc); Serial.print(" OP="); hex2(Serial, r.opcode); Serial.print(" MC="); Serial.println(r.cycles); sendState(); return; }
  if (!strcasecmp(cmd, "SETPC")) { char *a = strtok(nullptr, " \t"); uint32_t v = 0; if (!parseHex(a, 0xffffu, v)) { monitorError("SETPC"); return; } stopRun(); cpu.setPC((uint16_t)v); monitorLine("SETPC"); return; }
  if (!strcasecmp(cmd, "SPEED")) { char *s = strtok(nullptr, " \t"); uint32_t v = 0; if (!parseDec(s, 1000u, 32000000u, v)) { monitorError("SPEED"); return; } cpuHz = v; clockBudget = 0; lastTickUs = micros(); monitorLine("SPEED"); return; }
  if (!strcasecmp(cmd, "PEEK")) {
    char *as = strtok(nullptr, " \t"), *cs = strtok(nullptr, " \t"); uint32_t a = 0, count = 1;
    if (!parseHex(as, 0xffffu, a) || (cs && !parseHex(cs, 0x40u, count)) || count == 0) { monitorError("PEEK"); return; }
    monitorPrefix('='); Serial.print("MEM "); hex4(Serial, (uint16_t)a); Serial.print(' '); for (uint32_t i = 0; i < count; ++i) { if (i) Serial.print(' '); hex2(Serial, bus.read((uint16_t)(a + i))); } Serial.println(); return;
  }
  if (!strcasecmp(cmd, "POKE")) {
    char *as = strtok(nullptr, " \t"); uint32_t a = 0; if (!parseHex(as, 0xffffu, a)) { monitorError("POKE"); return; } unsigned n = 0;
    for (char *bs = strtok(nullptr, " \t"); bs; bs = strtok(nullptr, " \t")) { uint32_t b = 0; if (!parseHex(bs, 0xffu, b) || n >= 0x40u) { monitorError("POKE"); return; } bus.write((uint16_t)(a + n), (uint8_t)b); ++n; }
    if (!n) { monitorError("POKE"); return; } monitorLine("POKE"); return;
  }
  if (!strcasecmp(cmd, "IO")) {
    char *which = strtok(nullptr, " \t"), *vs = strtok(nullptr, " \t"); uint32_t v = 0; if (!which || !vs) { monitorError("IO"); return; }
    if (!strcasecmp(which, "SW")) { if (!parseHex(vs, 0xffu, v)) { monitorError("IO SW"); return; } bus.switches = (uint8_t)v; }
    else if (!strcasecmp(which, "ADC0")) { if (!parseDec(vs, 0, 1023, v)) { monitorError("IO ADC0"); return; } bus.adc[0] = (uint16_t)v; }
    else if (!strcasecmp(which, "ADC1")) { if (!parseDec(vs, 0, 1023, v)) { monitorError("IO ADC1"); return; } bus.adc[1] = (uint16_t)v; }
    else { monitorError("IO"); return; }
    sendIo(); return;
  }
  monitorError("UNKNOWN");
}
void serviceSerial() {
  while (Serial.available()) {
    const int iv = Serial.read(); if (iv < 0) break; const uint8_t b = (uint8_t)iv;
    switch (rxMode) {
      case RxMode::Guest: if (b == GS) rxMode = RxMode::Escape; else bus.enqueue(b); break;
      case RxMode::Escape: if (b == GS) { bus.enqueue(GS); rxMode = RxMode::Guest; } else if (b == '!') { monitorLen = 0; rxMode = RxMode::Monitor; } else rxMode = RxMode::Guest; break;
      case RxMode::Monitor:
        if (b == '\r') break;
        if (b == '\n') { monitorBuf[monitorLen] = 0; handleMonitor(monitorBuf); monitorLen = 0; rxMode = RxMode::Guest; }
        else if (monitorLen + 1 < sizeof(monitorBuf)) monitorBuf[monitorLen++] = (char)b;
        else { monitorLen = 0; rxMode = RxMode::Guest; monitorError("LINE TOO LONG"); }
        break;
    }
  }
}
}
void setup() {
  bus.beginQ(); bus.attach(&cpu); Serial.begin(115200); while (!Serial && millis() < 5000) delay(10);
  monitorLine("COSMAC1802 NATIVE FORTH V8 READY"); coldForth();
}
void loop() { serviceSerial(); tickCpu(); }
