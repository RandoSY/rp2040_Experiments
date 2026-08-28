#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <deque>
#include <string>
#include "cdp1802.h"
#include "native_rom_v9_split.h"

class TestBus : public CDP1802Bus {
public:
  uint8_t memory[65536]{};
  std::deque<uint8_t> rx;
  std::string tx;
  bool q = false;
  CDP1802 *cpu = nullptr;

  void attach(CDP1802 *p) { cpu = p; updateEf4(); }
  uint8_t read(uint16_t a) override { return memory[a]; }
  void write(uint16_t a, uint8_t v) override { if (a < MF_ROM_START || a >= MF_ROM_END) memory[a] = v; }
  uint8_t input(uint8_t p) override {
    p &= 7u;
    if (p == 7) { if (rx.empty()) { updateEf4(); return 0; } uint8_t v = rx.front(); rx.pop_front(); updateEf4(); return v; }
    if (p == 1) return 3;
    if (p == 2) return 0x34;
    if (p == 3) return 0x02;
    return 0;
  }
  void output(uint8_t p, uint8_t v) override { if ((p & 7u) == 7) tx.push_back((char)v); }
  void qChanged(bool v) override { q = v; }
  static int hn(char c) { if (c >= '0' && c <= '9') return c-'0'; if (c >= 'A' && c <= 'F') return c-'A'+10; if (c >= 'a' && c <= 'f') return c-'a'+10; return -1; }
  void install() {
    memset(memory, 0, sizeof(memory));
    for (uint16_t s=0; s<MF_ROM_SEGMENT_COUNT; ++s) { const auto &seg=MF_ROM_SEGMENTS[s]; for (uint16_t i=0; i<seg.length; ++i) { int h=hn(seg.hex[2*i]), l=hn(seg.hex[2*i+1]); assert(h>=0 && l>=0); memory[(uint16_t)(seg.address+i)]=(uint8_t)((h<<4)|l); } }
    rx.clear(); tx.clear(); updateEf4();
  }
  void enqueue(const std::string &s) { for (unsigned char c : s) rx.push_back(c); updateEf4(); }
  void updateEf4() { if (cpu) cpu->setEF(4, !rx.empty()); }
};

static bool endsWith(const std::string &s,const char *suffix){size_t n=strlen(suffix);return s.size()>=n&&s.compare(s.size()-n,n,suffix)==0;}
static void runUntilPrompt(CDP1802 &cpu,TestBus &bus,uint64_t maxSteps=3000000){for(uint64_t i=0;i<maxSteps;++i){cpu.step();if(bus.rx.empty()&&(endsWith(bus.tx,"OK\r\n")||endsWith(bus.tx,"?\r\n")))return;assert(!cpu.halted);}fprintf(stderr,"timeout PC=%04X output=%s\n",cpu.pc(),bus.tx.c_str());assert(false);}
static std::string cmd(CDP1802 &cpu,TestBus &bus,const char *line){bus.tx.clear();bus.enqueue(std::string(line)+"\r");runUntilPrompt(cpu,bus);return bus.tx;}
static void contains(const std::string&s,const char*n){if(s.find(n)==std::string::npos){fprintf(stderr,"missing [%s] in [%s]\n",n,s.c_str());assert(false);}}

int main(){
  TestBus bus;CDP1802 cpu(bus);bus.attach(&cpu);bus.install();cpu.hardReset();cpu.setPC(MF_COLD);runUntilPrompt(cpu,bus,200000);contains(bus.tx,"COSMAC 1802 microFORTH\r\nOK\r\n");
  contains(cmd(cpu,bus,"2 2 + ."),"4 OK\r\n");
  contains(cmd(cpu,bus,"3 1 AND ."),"1 OK\r\n");contains(cmd(cpu,bus,"2 1 OR ."),"3 OK\r\n");contains(cmd(cpu,bus,"3 1 XOR ."),"2 OK\r\n");
  contains(cmd(cpu,bus,"1 2 3 ROT . . ."),"1 3 2 OK\r\n");
  contains(cmd(cpu,bus,"0 0< ."),"0 OK\r\n");contains(cmd(cpu,bus,"-1 0< ."),"1 OK\r\n");
  contains(cmd(cpu,bus,"2 2 = ."),"1 OK\r\n");contains(cmd(cpu,bus,"2 3 <> ."),"1 OK\r\n");contains(cmd(cpu,bus,"2 3 < ."),"1 OK\r\n");contains(cmd(cpu,bus,"3 2 > ."),"1 OK\r\n");
  contains(cmd(cpu,bus,"1 28672 ! 2 28672 +! 28672 @ ."),"3 OK\r\n");
  contains(cmd(cpu,bus,"5 0 / ."),"?DIV0 0 OK\r\n");
  contains(cmd(cpu,bus,": X 1 ;"),"OK\r\n");contains(cmd(cpu,bus,"X ."),"1 OK\r\n");contains(cmd(cpu,bus,": X 2 ;"),"OK\r\n");contains(cmd(cpu,bus,"X ."),"2 OK\r\n");
  contains(cmd(cpu,bus,": BAD NOSUCH ;"),"?\r\n");contains(cmd(cpu,bus,"BAD ."),"?\r\n");
  contains(cmd(cpu,bus,"2 3 and ."),"2 OK\r\n");contains(cmd(cpu,bus,": sq dup * ;"),"OK\r\n");contains(cmd(cpu,bus,"12 sq ."),"144 OK\r\n");
  std::string words=cmd(cpu,bus,"WORDS");contains(words,"AND OR XOR");contains(words,"ROT");contains(words,"+!");contains(words,"0<");
  cmd(cpu,bus,"QON");assert(bus.q);cmd(cpu,bus,"QOFF");assert(!bus.q);
  puts("native terminal v9 regression: PASS");return 0;
}
