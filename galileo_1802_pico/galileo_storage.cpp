#ifdef ARDUINO
#include "galileo_storage.h"
#include <LittleFS.h>
#include "crc32.h"

static const char *ROM_PATH="/galileo.rom";
static const char *RAM_PATH="/galileo.ram";

bool GalileoImageStore::begin() { return LittleFS.begin(); }
bool GalileoImageStore::haveRom() const { return LittleFS.exists(ROM_PATH); }
bool GalileoImageStore::haveRam() const { return LittleFS.exists(RAM_PATH); }

bool GalileoImageStore::saveFile(const char *path, const uint8_t *data, size_t len, Print *log) {
    String tmp = String(path) + ".tmp";
    LittleFS.remove(tmp.c_str());
    File f=LittleFS.open(tmp.c_str(),"w");
    if (!f) { if(log)log->println("ERR flash open"); return false; }
    size_t n=f.write(data,len); f.flush(); f.close();
    if (n!=len) { LittleFS.remove(tmp.c_str()); if(log)log->println("ERR flash short write"); return false; }
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp.c_str(),path)) { LittleFS.remove(tmp.c_str()); if(log)log->println("ERR flash rename"); return false; }
    return true;
}

bool GalileoImageStore::saveRom(const uint8_t *data, size_t len, Print *log) {
    if (len!=GalileoMachine::ROM_SIZE || crc32_ieee(data,len)!=GalileoMachine::EXPECTED_ROM_CRC) {
        if(log)log->println("ERR ROM size/CRC"); return false;
    }
    return saveFile(ROM_PATH,data,len,log);
}
bool GalileoImageStore::saveRam(const uint8_t *data, size_t len, Print *log) {
    if (len!=GalileoMachine::RAM_INIT_SIZE || crc32_ieee(data,len)!=GalileoMachine::EXPECTED_RAM_CRC) {
        if(log)log->println("ERR RAM size/CRC"); return false;
    }
    return saveFile(RAM_PATH,data,len,log);
}

bool GalileoImageStore::load(GalileoMachine &machine, Print *log) {
    if (!haveRom() || !haveRam()) { if(log)log->println("ERR images not installed"); return false; }
    static uint8_t rom[GalileoMachine::ROM_SIZE];
    static uint8_t ram[GalileoMachine::RAM_INIT_SIZE];
    File rf=LittleFS.open(ROM_PATH,"r"); File mf=LittleFS.open(RAM_PATH,"r");
    if (!rf || !mf || rf.size()!=GalileoMachine::ROM_SIZE || mf.size()!=GalileoMachine::RAM_INIT_SIZE) {
        if(rf)rf.close(); if(mf)mf.close(); if(log)log->println("ERR stored image size"); return false;
    }
    size_t rn=rf.read(rom,sizeof(rom)); size_t mn=mf.read(ram,sizeof(ram)); rf.close(); mf.close();
    uint32_t rc=0,mc=0;
    bool ok=rn==sizeof(rom)&&mn==sizeof(ram)&&machine.loadImages(rom,sizeof(rom),ram,sizeof(ram),&rc,&mc);
    if(log){
        if(ok){log->print("ROM CRC ");log->print(rc,HEX);log->println(" OK");log->print("RAM CRC ");log->print(mc,HEX);log->println(" OK");}
        else log->println("ERR stored image CRC");
    }
    return ok;
}

bool GalileoImageStore::eraseAll(Print *log) {
    bool a=!LittleFS.exists(ROM_PATH)||LittleFS.remove(ROM_PATH);
    bool b=!LittleFS.exists(RAM_PATH)||LittleFS.remove(RAM_PATH);
    if(log)log->println((a&&b)?"OK images erased":"ERR erase");
    return a&&b;
}
#endif
