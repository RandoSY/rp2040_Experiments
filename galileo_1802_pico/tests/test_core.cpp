#include "../cdp1802.h"
#include "../galileo_machine.h"
#include "../gy271_math.h"
#include "../gy271_sensor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct RAMBus: CDP1802Bus { uint8_t m[65536]{}; uint8_t read(uint16_t a) override{return m[a];} void write(uint16_t a,uint8_t v) override{m[a]=v;} };
static int pass=0,fail=0;
#define CHECK(x,msg) do{if(x){pass++;}else{fail++;fprintf(stderr,"FAIL: %s\n",msg);}}while(0)

int main(){
  {
    RAMBus b; CDP1802 c(b); CHECK(c.IE==1&&c.P==0&&c.X==0&&c.R[0]==0,"reset state");
    b.m[0]=0xF8;b.m[1]=1;b.m[2]=0xFC;b.m[3]=2;b.m[4]=0;
    c.step();c.step();c.step(); CHECK(c.D==3&&c.idle,"LDI/ADI/IDL");
    c.R[5]=0x1234;c.P=5;c.X=7;c.IE=1;c.idle=true; CHECK(c.interrupt(),"interrupt accepted");
    CHECK(c.T==0x75&&c.P==1&&c.X==2&&c.IE==0&&!c.idle,"interrupt state");
  }
  {
    RAMBus b; CDP1802 c(b); c.hardReset(); c.X=2;c.R[2]=0x100;b.m[0]=0x70;b.m[0x100]=0x34;c.IE=0;c.step();
    CHECK(c.X==3&&c.P==4&&c.IE==1&&c.R[2]==0x101,"RET restores X/P + IE");
  }
  {
    RAMBus b; CDP1802 c(b); c.hardReset(); c.X=2;c.R[2]=0x100;b.m[0]=0x71;b.m[0x100]=0x34;c.IE=1;c.step();
    CHECK(c.X==3&&c.P==4&&c.IE==0&&c.R[2]==0x101,"DIS restores X/P + clears IE");
  }
  {
    RAMBus b; CDP1802 c(b); c.hardReset(); b.m[0]=0xCC;b.m[1]=0xAA;b.m[2]=0xBB;b.m[3]=0xF8;b.m[4]=0x42;c.IE=1;c.step();
    CHECK(c.pc()==3,"LSIE skips two bytes");c.step();CHECK(c.D==0x42,"LSIE landing");
  }
  {
    GalileoMachine g; g.write(0x74F1,0xAB); CHECK(g.status7002()==0x02,"status $7002 power");
    g.write(0x7200,12);g.write(0x7201,13);CHECK(g.read(0x7202)==156&&g.read(0x7203)==0,"8x8 multiplier");
    g.ram[0]=0xA5;g.rom[0]=0x5A;g.write(0x74F0,0xAB);CHECK(g.read(0)==0xA5&&g.read(0x4000)==0x5A,"bank 0/4 swap");
    g.write(0x74F0,0xAA);g.ram[0]=0x11;g.write(0x7700,0xAB);g.write(0x4000,0x22);CHECK(g.ram[0]==0x11,"RAM protection blocks write");
    g.write(0x7700,0xAA);g.write(0x4000,0x22);CHECK(g.ram[0]==0x22,"RAM protection release");
    g.write(0x74F8,0xAB);CHECK(!g.switches[8],"flip ignored without power");g.write(0x74F6,0xAB);g.write(0x74F8,0xAB);CHECK(g.switches[8],"flip accepted with power");g.write(0x74F9,0xAB);CHECK(!g.switches[8]&&g.switches[9],"flip interlock");g.write(0x74F6,0xAA);CHECK(!g.switches[8]&&!g.switches[9],"flip power off clears directions");
    CHECK(g.read(0x7033)==0x70,"ENABLE-INT code field");
    g.setRealFieldMilliUt(10000,-20000,50000,true,123);
    g.useRealMag(true);
    CHECK(g.adcFor(0)==2148 && g.adcFor(1)==1848 && g.adcFor(2)==2548,"real MAG INB ADC mapping");
    CHECK(g.adcFor(4)==2148 && g.adcFor(5)==1848 && g.adcFor(6)==2548,"real MAG OUT ADC mapping");
    g.setRealFieldMilliUt(0,0,0,false,124);
    CHECK(g.adcFor(0)==2048 && g.adcFor(4)==2048,"invalid real MAG fails to midscale");
    g.useRealMag(false);
    CHECK(g.adcFor(0)==2168 && g.adcFor(4)==2148,"SIM fixture preserved");
  }
  {
    CHECK(gy271_hmc_milli_ut(1090)==100000,"HMC 1090 LSB/G -> 100 uT");
    CHECK(gy271_hmc_milli_ut(-1090)==-100000,"HMC signed conversion");
    CHECK(gy271_qmc_milli_ut(12000)==100000,"QMC 12000 LSB/G -> 100 uT");
    CHECK(gy271_qmc_milli_ut(-12000)==-100000,"QMC signed conversion");
    GY271Telemetry in; in.chip=GY271Chip::QMC5883L;in.configured=true;in.valid=true;in.rawX=123;in.milliUtX=4567;in.sampleCount=9;
    g_gy271Mailbox.publish(in); GY271Telemetry out;
    CHECK(g_gy271Mailbox.snapshot(out)&&out.chip==GY271Chip::QMC5883L&&out.rawX==123&&out.milliUtX==4567&&out.sampleCount==9,"core1/core0 MAG mailbox");
  }
  printf("%d passed, %d failed\n",pass,fail); return fail?1:0;
}
