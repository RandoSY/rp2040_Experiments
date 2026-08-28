#include <Arduino.h>
#include "cdp1802.h"
#include "cdp1802_serial_monitor.h"
#include "galileo_machine.h"
#include "galileo_flight.h"
#include "galileo_storage.h"
#include "crc32.h"
#include "gy271_sensor.h"

class PicoGalileoMachine : public GalileoMachine {
public:
  void beginQIndicator(){
    pinMode(LED_BUILTIN,OUTPUT);
    qIndicatorReady_=true;
    digitalWrite(LED_BUILTIN,qLevel_?HIGH:LOW);
  }

  void qChanged(bool q) override {
    qLevel_=q;
    if(qIndicatorReady_) digitalWrite(LED_BUILTIN,q?HIGH:LOW);
  }

private:
  bool qIndicatorReady_=false;
  bool qLevel_=false;
};

static PicoGalileoMachine machine;
static CDP1802 cpu(machine);
static GalileoFlightBridge flight(machine);
static GalileoImageStore imageStore;
static GY271Sensor gy271;

static bool flightRunning=false;
static bool imagesReady=false;
static uint32_t lastTickUs=0;
static uint32_t lastIrqUs=0;
static int64_t clockBudgetTenths=0;
static constexpr uint32_t IRQ_PERIOD_US=33333;
static constexpr int64_t MIN_INSTR_TENTHS=160;

static uint16_t fstack[128];
static size_t fdepth=0;
static int forthBase=10;

static char linebuf[320];
static size_t linelen=0;

struct UploadState {
  bool active=false;
  bool isRom=false;
  size_t expected=0;
  uint32_t expectedCrc=0;
  uint8_t data[GalileoMachine::ROM_SIZE]{};
  bool touched[GalileoMachine::ROM_SIZE]{};
} upload;

static void printHex2(uint8_t v){ if(v<0x10)Serial.print('0'); Serial.print(v,HEX); }
static void printHex4(uint16_t v){ printHex2((uint8_t)(v>>8)); printHex2((uint8_t)v); }
static bool ieq(const char *a,const char *b){ while(*a&&*b){char x=toupper((unsigned char)*a++),y=toupper((unsigned char)*b++);if(x!=y)return false;}return *a==0&&*b==0; }

static void printMilliUt(int32_t milliUt){
  if(milliUt<0){Serial.print('-');milliUt=-milliUt;}
  Serial.print(milliUt/1000); Serial.print('.');
  int32_t frac=milliUt%1000; if(frac<100)Serial.print('0');if(frac<10)Serial.print('0');Serial.print(frac);
}

static bool getMagTelemetry(GY271Telemetry &t){ return g_gy271Mailbox.snapshot(t); }

static void syncMagToMachine(){
  GY271Telemetry t;
  if(getMagTelemetry(t)) machine.setRealFieldMilliUt(t.milliUtX,t.milliUtY,t.milliUtZ,t.valid,t.sampleCount);
}

static void printMagTelemetry(){
  GY271Telemetry t; if(!getMagTelemetry(t)){Serial.println("MAG mailbox busy");return;}
  Serial.print("MAG chip=");Serial.print(gy271ChipName(t.chip));Serial.print(" addr=0x");printHex2(t.address);
  Serial.print(" configured=");Serial.print(t.configured?"yes":"no");Serial.print(" valid=");Serial.print(t.valid?"yes":"no");
  Serial.print(" backend=");Serial.println(machine.usingRealMag()?"REAL":"SIM");
  Serial.print("RAW X=");Serial.print(t.rawX);Serial.print(" Y=");Serial.print(t.rawY);Serial.print(" Z=");Serial.print(t.rawZ);
  Serial.print(" overflow=");Serial.println(t.overflow?"yes":"no");
  Serial.print("FIELD X=");printMilliUt(t.milliUtX);Serial.print(" uT  Y=");printMilliUt(t.milliUtY);Serial.print(" uT  Z=");printMilliUt(t.milliUtZ);Serial.println(" uT");
  Serial.print("samples=");Serial.print(t.sampleCount);Serial.print(" errors=");Serial.print(t.errorCount);Serial.print(" age_ms=");
  Serial.println(t.lastSampleMs?(uint32_t)(millis()-t.lastSampleMs):0);
  Serial.print("Galileo adapter: 0 uT -> 2048, ");Serial.print(machine.realCountsPerUt);Serial.println(" ADC counts/uT; one GY-271 feeds INB and OUT axes");
}

static void rescanMag(){
  const bool was=flightRunning; flightRunning=false;
  uint32_t request=g_gy271RescanRequest+1; g_gy271RescanRequest=request;
  uint32_t start=millis(); while(g_gy271RescanDone!=request && (uint32_t)(millis()-start)<1500u) delay(1);
  if(g_gy271RescanDone!=request){Serial.println("ERR MAG core-1 scan timeout");flightRunning=was;return;}
  syncMagToMachine(); printMagTelemetry(); flightRunning=was;
}

static void bootFlight(){
  if(!imageStore.load(machine,&Serial)){imagesReady=false;flightRunning=false;return;}
  imagesReady=true; cpu.hardReset(); flightRunning=true; lastTickUs=micros(); lastIrqUs=lastTickUs; clockBudgetTenths=0;
  Serial.println("GALILEO flight image booted at $0000");
}

static void printRegs(){
  for(int i=0;i<16;i++){Serial.print('R');Serial.print(i,HEX);Serial.print('=');printHex4(cpu.R[i]);Serial.print((i%4==3)?"\r\n":"  ");}
  Serial.print("D=");printHex2(cpu.D);Serial.print(" DF=");Serial.print(cpu.DF);Serial.print(" P=");Serial.print(cpu.P,HEX);Serial.print(" X=");Serial.print(cpu.X,HEX);
  Serial.print(" T=");printHex2(cpu.T);Serial.print(" IE=");Serial.print(cpu.IE);Serial.print(" Q=");Serial.print(cpu.Q);Serial.print(" PC=");printHex4(cpu.pc());
  Serial.print(" IDL=");Serial.print(cpu.idle);Serial.print(" HALT=");Serial.println(cpu.halted);
}

static void printStatus(){
  Serial.print("$7002=");printHex2(machine.status7002());Serial.print("  $7003=");printHex2(machine.status7003());
  Serial.print("  ADC ch=");Serial.print(machine.selectedChannel);Serial.print(" value=");printHex4(machine.adc);
  Serial.print("  MUL=");printHex4(machine.mulResult);Serial.print("  IRQ=");Serial.println(machine.interruptCount);
  const char *names[]={"MEMSW","IN PWR","OUT PWR","IN HI","OUT HI","CAL","FLIP PWR","1802 DIS","IN LEFT","OUT LEFT","IN RIGHT","OUT RIGHT"};
  for(int i=0;i<12;i++){Serial.print(names[i]);Serial.print('=');Serial.print(machine.switches[i]?"ON":"off");Serial.print((i%4==3)?"\r\n":"  ");}
}

static void tickFlight(){
  syncMagToMachine();
  if(!flightRunning||!imagesReady||cpu.halted)return;
  uint32_t now=micros(); uint32_t elapsed=now-lastTickUs; lastTickUs=now; if(elapsed>50000)elapsed=50000;
  clockBudgetTenths += (int64_t)elapsed*16;
  while(clockBudgetTenths>=MIN_INSTR_TENTHS && !cpu.halted){
    if(cpu.idle)break;
    CDP1802Step r=cpu.step(); if(!r.cycles)break; clockBudgetTenths-=(int64_t)r.cycles*80;
  }
  now=micros();
  while((uint32_t)(now-lastIrqUs)>=IRQ_PERIOD_US){
    lastIrqUs+=IRQ_PERIOD_US;
    machine.injectInterrupt(cpu,true);
  }
}

static bool parseNumber(const char *s,uint16_t &out){
  if(!s||!*s)return false; int base=forthBase; const char *p=s; bool neg=false;
  if(*p=='-'){neg=true;p++;}
  if(p[0]=='$'){base=16;p++;} else if(p[0]=='0'&&(p[1]=='x'||p[1]=='X')){base=16;p+=2;}
  char *end=nullptr; unsigned long v=strtoul(p,&end,base); if(!end||*end)return false;
  int32_t sv=neg?-(int32_t)v:(int32_t)v; out=(uint16_t)sv; return true;
}
static bool push(uint16_t v){if(fdepth>=128){Serial.println("? STACK OVERFLOW");return false;}fstack[fdepth++]=v;return true;}
static bool pop(uint16_t &v){if(!fdepth){Serial.println("? STACK UNDERFLOW");return false;}v=fstack[--fdepth];return true;}

static void words(){
  Serial.println("HOST: HEX DECIMAL . U. .S WORDS GSTATUS C@ C! @ !");
  Serial.print("FLIGHT [verified CALL]: ");
  for(size_t i=0;i<GALILEO_WORD_COUNT;i++){Serial.print(GALILEO_WORDS[i].name);Serial.print(' ');} Serial.println();
  Serial.println("MONITOR: .RUN .PAUSE .RESET .REGS .STATUS .STEP .BURN n .CMD fn val .ERASE");
  Serial.println("MAG: .MAGSCAN .MAGSTATUS .MAGRAW .MAGREAL .MAGSIM");
  Serial.println("1802 DEBUG: @HELP @STATE @STEP @RESET @IRQ @SETPC @SET @EF @PEEK @POKE");
}

static void execForthToken(char *tok){
  if(ieq(tok,"HEX")){forthBase=16;return;} if(ieq(tok,"DECIMAL")){forthBase=10;return;}
  if(ieq(tok,".")){uint16_t v;if(pop(v))Serial.println((int16_t)v);return;}
  if(ieq(tok,"U.")){uint16_t v;if(pop(v))Serial.println(v);return;}
  if(ieq(tok,".S")){Serial.print('<');Serial.print(fdepth);Serial.print("> ");for(size_t i=0;i<fdepth;i++){if(forthBase==16){printHex4(fstack[i]);}else Serial.print((int16_t)fstack[i]);Serial.print(' ');}Serial.println();return;}
  if(ieq(tok,"WORDS")){words();return;} if(ieq(tok,"GSTATUS")){printStatus();return;}
  if(ieq(tok,"C@")){uint16_t a;if(pop(a))push(machine.read(a));return;}
  if(ieq(tok,"C!")){uint16_t a,v;if(pop(a)&&pop(v))machine.write(a,(uint8_t)v);return;}
  if(ieq(tok,"@")){uint16_t a;if(pop(a))push((uint16_t)(((uint16_t)machine.read(a)<<8)|machine.read(a+1)));return;}
  if(ieq(tok,"!")){uint16_t a,v;if(pop(a)&&pop(v)){machine.write(a,(uint8_t)(v>>8));machine.write(a+1,(uint8_t)v);}return;}
  uint16_t n;if(parseNumber(tok,n)){push(n);return;}
  const GalileoWord *w=findGalileoWord(tok);
  if(w){bool was=flightRunning;flightRunning=false;size_t d=fdepth;if(flight.invoke(*w,fstack,d,128)){fdepth=d;Serial.print("[flight ");Serial.print(w->name);Serial.print(" ");Serial.print(flight.lastSteps());Serial.println(" insn]");}else{Serial.print("? FLIGHT ");Serial.println(flight.lastError());}flightRunning=was;return;}
  Serial.print("? ");Serial.println(tok);
}

static void handleMonitor(char *line){
  char *cmd=strtok(line," \t"); if(!cmd)return;
  if(ieq(cmd,".RUN")){flightRunning=imagesReady;Serial.println("OK RUN");return;}
  if(ieq(cmd,".PAUSE")){flightRunning=false;Serial.println("OK PAUSE");return;}
  if(ieq(cmd,".RESET")){bootFlight();return;}
  if(ieq(cmd,".REGS")){printRegs();return;}
  if(ieq(cmd,".STATUS")){printStatus();return;}
  if(ieq(cmd,".STEP")){flightRunning=false;CDP1802Step r=cpu.step();Serial.print("PC ");printHex4(r.pc);Serial.print(" OP ");printHex2(r.opcode);Serial.print(" -> ");printHex4(cpu.pc());Serial.println();return;}
  if(ieq(cmd,".BURN")){char *s=strtok(nullptr," \t");uint32_t n=s?strtoul(s,nullptr,0):100000;flightRunning=false;uint32_t done=0;while(done<n&&!cpu.halted){if(cpu.idle){if(!machine.injectInterrupt(cpu,true))break;done++;continue;}CDP1802Step r=cpu.step();done++;if(!r.cycles&&!cpu.idle)break;if(done%20000==0)machine.injectInterrupt(cpu,true);}Serial.print("BURN ");Serial.print(done);Serial.print(" PC ");printHex4(cpu.pc());Serial.print(" HALT ");Serial.println(cpu.halted);return;}
  if(ieq(cmd,".CMD")){
    char *a=strtok(nullptr," \t"),*b=strtok(nullptr," \t");
    if(!a||!b){Serial.println("ERR .CMD fn value");return;}
    if(!imagesReady){Serial.println("ERR images not loaded");return;}
    if(machine.ram[0x0FF0] < 0x20){Serial.println("ERR flight command ring not initialized; run/burn flight first");return;}
    char *ea=nullptr,*eb=nullptr;
    unsigned long f=strtoul(a,&ea,16),v=strtoul(b,&eb,16);
    if(!ea||*ea||!eb||*eb||f>0xFF||v>0xFF){Serial.println("ERR .CMD expects two hex bytes");return;}
    const uint8_t fn=(uint8_t)f,val=(uint8_t)v;
    const bool was=flightRunning; flightRunning=false;
    machine.ram[0x0E40]=fn; machine.ram[0x0E41]=val;
    const GalileoWord *ck=findGalileoWord("CKCOMM");
    bool ok=flight.runIrqSubroutine(0x0BAA);
    if(ok&&ck){size_t d=0;uint16_t dummy[4]{};ok=flight.invoke(*ck,dummy,d,4);}
    if(ok){Serial.print("OK command path $74F2=");Serial.println(machine.switches[2]?"ON":"off");}
    else {Serial.print("ERR command path: ");Serial.println(flight.lastError());}
    flightRunning=was; return;
  }
  if(ieq(cmd,".MAGSCAN")){rescanMag();return;}
  if(ieq(cmd,".MAGSTATUS")||ieq(cmd,".MAGRAW")){syncMagToMachine();printMagTelemetry();return;}
  if(ieq(cmd,".MAGSIM")){machine.useRealMag(false);Serial.println("OK MAG backend SIM");return;}
  if(ieq(cmd,".MAGREAL")){
    GY271Telemetry t; if(!getMagTelemetry(t)||!t.configured||!t.valid){Serial.println("ERR no fresh verified GY-271 sample; run .MAGSCAN then .MAGSTATUS");return;}
    machine.useRealMag(true); syncMagToMachine();
    Serial.print("OK MAG backend REAL (");Serial.print(gy271ChipName(t.chip));Serial.println(")");return;
  }
  if(ieq(cmd,".ERASE")){flightRunning=false;imageStore.eraseAll(&Serial);imagesReady=false;return;}
  Serial.println("? monitor command");
}

static int hexNibble(char c){if(c>='0'&&c<='9')return c-'0';c=toupper((unsigned char)c);if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static void uploadLine(char *line){
  if(strncmp(line,"!BEGIN ",7)==0){char kind[8];unsigned len;unsigned long crc;if(sscanf(line+7,"%7s %u %lx",kind,&len,&crc)!=3){Serial.println("ERR BEGIN");return;}bool isRom=ieq(kind,"ROM");size_t need=isRom?GalileoMachine::ROM_SIZE:GalileoMachine::RAM_INIT_SIZE;if(len!=need){Serial.println("ERR LENGTH");return;}upload=UploadState();upload.active=true;upload.isRom=isRom;upload.expected=need;upload.expectedCrc=(uint32_t)crc;Serial.print("READY ");Serial.println(isRom?"ROM":"RAM");return;}
  if(strncmp(line,"!DATA ",6)==0){if(!upload.active){Serial.println("ERR NO BEGIN");return;}char *p=line+6;char *sp=strchr(p,' ');if(!sp){Serial.println("ERR DATA");return;}*sp++=0;size_t off=strtoul(p,nullptr,16);size_t idx=off;while(*sp){while(*sp==' ')sp++;if(!*sp)break;int h=hexNibble(*sp++);int l=hexNibble(*sp++);if(h<0||l<0||idx>=upload.expected){Serial.println("ERR HEX");return;}upload.data[idx]=(uint8_t)((h<<4)|l);upload.touched[idx]=true;idx++;}Serial.println("OK DATA");return;}
  if(strcmp(line,"!END")==0){if(!upload.active){Serial.println("ERR NO BEGIN");return;}for(size_t i=0;i<upload.expected;i++)if(!upload.touched[i]){Serial.println("ERR MISSING");upload.active=false;return;}uint32_t c=crc32_ieee(upload.data,upload.expected);if(c!=upload.expectedCrc){Serial.print("ERR CRC ");Serial.println(c,HEX);upload.active=false;return;}bool ok=upload.isRom?imageStore.saveRom(upload.data,upload.expected,&Serial):imageStore.saveRam(upload.data,upload.expected,&Serial);upload.active=false;Serial.println(ok?"OK STORED":"ERR STORE");return;}
  if(strcmp(line,"!BOOT")==0){bootFlight();Serial.println(imagesReady?"OK BOOT":"ERR BOOT");return;}
  Serial.println("ERR UPLOAD COMMAND");
}

static void handleLine(char *line){
  while(*line==' '||*line=='\t')line++; if(!*line)return;
  if(line[0]=='@'&&line[1]&&line[1]!=' '&&line[1]!='\t'){flightRunning=false;handleCdp1802MonitorCommand(Serial,cpu,machine,line);return;}
  if(line[0]=='!'){uploadLine(line);return;}
  if(line[0]=='.'){handleMonitor(line);return;}
  char *tok=strtok(line," \t");while(tok){execForthToken(tok);tok=strtok(nullptr," \t");}Serial.println("OK");
}

void setup1(){
  (void)gy271.begin(Wire,4,5);
  g_gy271RescanDone=g_gy271RescanRequest;
}

void loop1(){
  gy271.poll();
  delay(2);
}

void setup(){
  machine.beginQIndicator();
  Serial.begin(115200); while(!Serial && millis()<5000) delay(10);
  Serial.println("GALILEO MAG / RP2040 CDP1802 emulator");
  Serial.println("1802 core: IE/RET/DIS/IDL/interrupt semantics enabled");
  Serial.println("1802 Q output: onboard Pico LED follows Q (SEQ=ON, REQ=OFF)");
  Serial.println("GY-271 backend: GP4=SDA GP5=SCL, HMC5883L/QMC5883L autodetect on core 1");
  if(!imageStore.begin()){Serial.println("ERR LittleFS mount");return;}
  if(imageStore.haveRom()&&imageStore.haveRam())bootFlight();
  else {Serial.println("NO GALILEO IMAGES INSTALLED");Serial.println("Run tools/load_galileo_images.py, then reconnect.");}
  words(); Serial.println("OK");
}

void loop(){
  tickFlight();
  while(Serial.available()){
    int c=Serial.read(); if(c<0)break;
    if(c=='\r')continue;
    if(c=='\n'){linebuf[linelen]=0;handleLine(linebuf);linelen=0;}
    else if(linelen+1<sizeof(linebuf))linebuf[linelen++]=(char)c;
    else {linelen=0;Serial.println("ERR line too long");}
  }
}