#include "../cdp1802.h"
#include "../galileo_machine.h"
#include "../galileo_flight.h"
#include "../crc32.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>

static int pass=0,fail=0;
#define CHECK(x,msg) do{if(x){++pass;}else{++fail;std::fprintf(stderr,"FAIL: %s\n",msg);}}while(0)

static std::vector<uint8_t> readFile(const char *p){
    std::ifstream f(p,std::ios::binary);
    if(!f) return {};
    f.seekg(0,std::ios::end); auto n=f.tellg(); f.seekg(0);
    std::vector<uint8_t> v((size_t)n); f.read((char*)v.data(),n); return v;
}

static uint32_t burn(CDP1802 &cpu, GalileoMachine &g, uint32_t loops){
    uint32_t done=0;
    for(;done<loops && !cpu.halted;++done){
        if(cpu.idle){ g.injectInterrupt(cpu,true); continue; }
        auto r=cpu.step(); if(!r.cycles && !cpu.idle) break;
        if((done%20000u)==0) g.injectInterrupt(cpu,true);
    }
    return done;
}

int main(int argc,char **argv){
    if(argc!=3){std::fprintf(stderr,"usage: %s galileo_rom.bin galileo_ram_init.bin\n",argv[0]);return 2;}
    auto rom=readFile(argv[1]),ram=readFile(argv[2]);
    CHECK(rom.size()==GalileoMachine::ROM_SIZE,"ROM size");
    CHECK(ram.size()==GalileoMachine::RAM_INIT_SIZE,"RAM init size");
    CHECK(crc32_ieee(rom.data(),rom.size())==GalileoMachine::EXPECTED_ROM_CRC,"ROM CRC");
    CHECK(crc32_ieee(ram.data(),ram.size())==GalileoMachine::EXPECTED_RAM_CRC,"RAM CRC");
    if(fail){std::printf("%d passed, %d failed\n",pass,fail);return 1;}

    GalileoMachine g;
    CHECK(g.loadImages(rom.data(),rom.size(),ram.data(),ram.size()),"load verified images");
    CDP1802 cpu(g); cpu.hardReset();

    const uint16_t expected[]={0x0000,0x0002,0x0402,0x0802,0x0C02,0x0FB8};
    for(size_t i=0;i<sizeof(expected)/sizeof(expected[0]);++i){
        CHECK(cpu.pc()==expected[i],"early preserved-ROM boot PC");
        if(i+1<sizeof(expected)/sizeof(expected[0])){
            uint32_t guard=0; do{auto r=cpu.step(); if(!r.cycles&&cpu.idle)break;}while(cpu.pc()!=expected[i+1] && ++guard<2000);
        }
    }

    uint32_t b=burn(cpu,g,150000);
    CHECK(b==150000,"150K preserved-image initialization burn");
    CHECK(!cpu.halted,"flight CPU did not trap/halt");
    CHECK(g.interruptCount>0,"flight interrupts accepted");
    CHECK(g.selectedChannel<=15,"ADC accessed valid channel");
    CHECK(g.ram[0x0FF0]>=0x20,"flight command ring initialized at $4FF0");

    GalileoFlightBridge fb(g);
    uint16_t st[16]{}; size_t depth=1; st[0]=0;
    const GalileoWord *power=findGalileoWord("POWER");
    CHECK(power!=nullptr,"POWER dictionary metadata");
    if(power){
        CHECK(fb.invoke(*power,st,depth,16),"call preserved POWER word");
        CHECK(depth==1 && st[0]==0x74F1,"INB POWER returns $74F1");
    }

    g.ram[0x0E40]=0xC6; g.ram[0x0E41]=0x55;
    CHECK(fb.runIrqSubroutine(0x0BAA),"preserved ?COMND R1 subroutine");
    const GalileoWord *ck=findGalileoWord("CKCOMM"); depth=0;
    CHECK(ck!=nullptr,"CKCOMM dictionary metadata");
    if(ck) CHECK(fb.invoke(*ck,st,depth,16),"preserved CKCOMM Forth word");
    CHECK(g.switches[2],"C6 55 turns OUTBOARD MAG POWER on through preserved code");

    const uint8_t before=g.ram[0x20]; const bool swBefore=g.switches[2];
    CHECK(!fb.runIrqSubroutine(0x1234,64),"invalid IRQ helper rejected");
    CHECK(g.ram[0x20]==before && g.switches[2]==swBefore,"failed IRQ helper rolls state back");

    std::printf("%d passed, %d failed; PC=$%04X IRQ=%lu\n",pass,fail,cpu.pc(),(unsigned long)g.interruptCount);
    return fail?1:0;
}
