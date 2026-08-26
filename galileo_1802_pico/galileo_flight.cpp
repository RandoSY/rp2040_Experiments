#include "galileo_flight.h"
#include <string.h>
#include <ctype.h>

const GalileoWord GALILEO_WORDS[] = {
    {"ALL-OFF",      0x0531, true},
    {"ON",           0x0549, true},
    {"OFF",          0x0554, true},
    {"OUT",          0x055F, true},
    {"INB",          0x0563, true},
    {"POWER",        0x0567, true},
    {"HIGAIN",       0x0571, true},
    {"CALIBRATE",    0x057B, true},
    {"FLIPPER",      0x057F, true},
    {"FLIP",         0x0583, true},
    {"LEFT",         0x0587, true},
    {"RIGHT",        0x058F, true},
    {"MEM-PROTECT",  0x059C, true},
    {"DATA-STORE",   0x0E91, true},
    {"CKCOMM",       0x0EA6, true},
};
const size_t GALILEO_WORD_COUNT = sizeof(GALILEO_WORDS)/sizeof(GALILEO_WORDS[0]);

static bool sameWord(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

const GalileoWord *findGalileoWord(const char *name) {
    for (size_t i=0;i<GALILEO_WORD_COUNT;i++) if (sameWord(name, GALILEO_WORDS[i].name)) return &GALILEO_WORDS[i];
    return nullptr;
}

bool GalileoFlightBridge::packStack(const uint16_t *stack, size_t depth, uint16_t &sp) {
    const uint16_t top = 0x6300;
    if (depth > 128) { lastError_ = "stack too deep"; return false; }
    sp = top;
    for (size_t i=0;i<depth;i++) {
        sp = (uint16_t)(sp - 2);
        machine_.write(sp, (uint8_t)(stack[i] >> 8));
        machine_.write((uint16_t)(sp+1), (uint8_t)stack[i]);
    }
    return true;
}

bool GalileoFlightBridge::unpackStack(uint16_t sp, uint16_t *stack, size_t &depth, size_t capacity) {
    const uint16_t top = 0x6300;
    if (sp > top || ((top-sp)&1)) { lastError_ = "flight stack corruption"; return false; }
    size_t n = (top-sp)/2;
    if (n > capacity || n > 128) { lastError_ = "flight stack overflow"; return false; }
    size_t out=0;
    for (int32_t a=(int32_t)top-2; a >= (int32_t)sp; a-=2) {
        uint16_t v = (uint16_t)(((uint16_t)machine_.read((uint16_t)a)<<8) | machine_.read((uint16_t)(a+1)));
        stack[out++] = v;
    }
    depth = out;
    return true;
}

bool GalileoFlightBridge::invoke(const GalileoWord &word, uint16_t *stack, size_t &depth, size_t capacity,
                                 uint32_t maxSteps) {
    lastError_ = ""; lastSteps_ = 0;
    if (!word.callable) { lastError_ = "word not qualified for direct call"; return false; }
    if (!machine_.imagesLoaded) { lastError_ = "images not loaded"; return false; }
    if (machine_.bankSwapped()) { lastError_ = "bank 0/4 is swapped"; return false; }

    uint8_t ramBackup[GalileoMachine::RAM_SIZE];
    uint8_t hwBackup[0x1000];
    bool protectBackup[8];
    bool switchBackup[12];
    memcpy(ramBackup, machine_.ram, sizeof(ramBackup));
    memcpy(hwBackup, machine_.hwShadow, sizeof(hwBackup));
    memcpy(protectBackup, machine_.protect, sizeof(protectBackup));
    memcpy(switchBackup, machine_.switches, sizeof(switchBackup));
    uint8_t selectedBackup=machine_.selectedChannel, mulXBackup=machine_.mulX, mulYBackup=machine_.mulY;
    uint16_t adcBackup=machine_.adc, mulResultBackup=machine_.mulResult, counterBackup=machine_.counter;
    uint8_t scratchBackup[GalileoMachine::SCRATCH_SIZE]; memcpy(scratchBackup,machine_.scratch,sizeof(scratchBackup));

    auto rollback=[&](){
        memcpy(machine_.ram,ramBackup,sizeof(ramBackup)); memcpy(machine_.hwShadow,hwBackup,sizeof(hwBackup));
        memcpy(machine_.protect,protectBackup,sizeof(protectBackup)); memcpy(machine_.switches,switchBackup,sizeof(switchBackup));
        machine_.selectedChannel=selectedBackup; machine_.adc=adcBackup; machine_.mulX=mulXBackup; machine_.mulY=mulYBackup;
        machine_.mulResult=mulResultBackup; machine_.counter=counterBackup; memcpy(machine_.scratch,scratchBackup,sizeof(scratchBackup));
    };

    uint16_t sp=0;
    if (!packStack(stack,depth,sp)) { rollback(); return false; }
    const uint16_t cfa=(uint16_t)(word.printedAddress-2);
    machine_.write(0x6000,(uint8_t)(cfa>>8)); machine_.write(0x6001,(uint8_t)cfa);
    machine_.write(0x6002,0x60); machine_.write(0x6003,0xF0);
    machine_.write(0x60F0,0x60); machine_.write(0x60F1,0xF2);
    machine_.write(0x60F2,0x00);

    CDP1802 c(machine_);
    memset(c.R,0,sizeof(c.R));
    c.R[13]=0x6000;
    c.R[14]=sp;
    c.R[2]=0x6200;
    c.R[12]=0x4700;
    c.R[15]=0x0006;
    c.P=15; c.X=14; c.IE=0; c.idle=false; c.halted=false;

    uint32_t n=0;
    for (; n<maxSteps && !c.idle && !c.halted; ++n) {
        CDP1802Step r=c.step();
        if (r.cycles==0 && !c.idle) break;
    }
    lastSteps_=n;
    if (!c.idle || c.pc()!=0x60F3 || c.halted) {
        lastError_="flight word did not return through verified trampoline";
        rollback(); return false;
    }
    if (!unpackStack(c.R[14],stack,depth,capacity)) { rollback(); return false; }
    memcpy(machine_.scratch,scratchBackup,sizeof(scratchBackup));
    return true;
}

bool GalileoFlightBridge::runIrqSubroutine(uint16_t entry, uint32_t maxSteps) {
    lastError_=""; lastSteps_=0;
    if (!machine_.imagesLoaded) { lastError_="images not loaded"; return false; }
    if (machine_.bankSwapped()) { lastError_="bank 0/4 is swapped"; return false; }

    uint8_t ramBackup[GalileoMachine::RAM_SIZE];
    uint8_t hwBackup[0x1000];
    bool protectBackup[8];
    bool switchBackup[12];
    uint8_t scratchBackup[GalileoMachine::SCRATCH_SIZE];
    memcpy(ramBackup, machine_.ram, sizeof(ramBackup));
    memcpy(hwBackup, machine_.hwShadow, sizeof(hwBackup));
    memcpy(protectBackup, machine_.protect, sizeof(protectBackup));
    memcpy(switchBackup, machine_.switches, sizeof(switchBackup));
    memcpy(scratchBackup, machine_.scratch, sizeof(scratchBackup));
    const uint8_t selectedBackup=machine_.selectedChannel, mulXBackup=machine_.mulX, mulYBackup=machine_.mulY;
    const uint16_t adcBackup=machine_.adc, mulResultBackup=machine_.mulResult, counterBackup=machine_.counter;

    auto rollback=[&](){
        memcpy(machine_.ram,ramBackup,sizeof(ramBackup));
        memcpy(machine_.hwShadow,hwBackup,sizeof(hwBackup));
        memcpy(machine_.protect,protectBackup,sizeof(protectBackup));
        memcpy(machine_.switches,switchBackup,sizeof(switchBackup));
        memcpy(machine_.scratch,scratchBackup,sizeof(scratchBackup));
        machine_.selectedChannel=selectedBackup; machine_.adc=adcBackup;
        machine_.mulX=mulXBackup; machine_.mulY=mulYBackup; machine_.mulResult=mulResultBackup;
        machine_.counter=counterBackup;
    };

    machine_.write(0x60F2,0x00);
    CDP1802 c(machine_); memset(c.R,0,sizeof(c.R));
    c.R[1]=0x60F2; c.R[3]=entry; c.P=3; c.X=2; c.IE=0; c.idle=false; c.halted=false;
    uint32_t n=0;
    for (; n<maxSteps && !c.idle && !c.halted; ++n) {
        CDP1802Step r=c.step(); if (r.cycles==0 && !c.idle) break;
    }
    lastSteps_=n;
    const bool ok=c.idle && c.pc()==0x60F3 && !c.halted;
    if (!ok) {
        lastError_="IRQ subroutine did not return through R1 trampoline";
        rollback();
        return false;
    }
    memcpy(machine_.scratch,scratchBackup,sizeof(scratchBackup));
    return true;
}
