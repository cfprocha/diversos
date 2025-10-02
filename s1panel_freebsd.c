// s1panel_freebsd.c — ACEMAGIC S1 (Holtek 04D9:FD01) — FreeBSD/OPNsense
// Compilar (em FreeBSD) e copiar o binário para o OPNsense:
//   cc -O2 -Wall -I/usr/local/include -L/usr/local/lib -lusb \
//      -o /usr/local/bin/s1panel_freebsd /usr/local/src/s1panel_freebsd.c

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <err.h>
#include <libusb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define VID 0x04d9
#define PID 0xfd01

// Painel: 320x170 (LANDSCAPE), RGB565 LITTLE-ENDIAN (lo,hi)
#define LCD_W 320
#define LCD_H 170
#define FB_SIZE (LCD_W*LCD_H*2)

#define CHUNK_DATA   4096
#define CHUNKS_FULL  26
#define CHUNK_LAST   2304
#define TOTAL_CHUNKS 27

// ---- fonte 5x7 (subset) ----
static const uint8_t font5x7[][7] = {
 {0x00,0x00,0x00,0x00,0x00}, // [32] espaço
 {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
 {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
 {0x00,0x36,0x36,0x00,0x00}, // ':'
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
 {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
 {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
 {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
 {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
 {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
 {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
 {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
 {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
 {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
};

static inline uint16_t RGB565(uint8_t r8, uint8_t g8, uint8_t b8){
    uint16_t r = (r8 >> 3) & 0x1F;
    uint16_t g = (g8 >> 2) & 0x3F;
    uint16_t b = (b8 >> 3) & 0x1F;
    return (uint16_t)((r<<11) | (g<<5) | b);
}
static inline void putpx(uint8_t *fb, int x, int y, uint16_t c){
    if (x<0||x>=LCD_W||y<0||y>=LCD_H) return;
    size_t off = (y*LCD_W + x)*2;
    fb[off]   = (uint8_t)(c & 0xFF);   // little-endian
    fb[off+1] = (uint8_t)(c >> 8);
}
static void fb_clear(uint8_t *fb, uint16_t c){
    uint8_t lo = c & 0xFF, hi = c >> 8;
    for (size_t i=0;i<FB_SIZE;i+=2){ fb[i]=lo; fb[i+1]=hi; }
}
static void draw_char(uint8_t *fb, int x, int y, char ch, int scale, uint16_t fg, uint16_t bg){
    const uint8_t *g=NULL;
    if (ch==' '){
        for(int dx=0; dx<6*scale; ++dx)
            for(int dy=0; dy<7*scale; ++dy)
                putpx(fb, x+dx, y+dy, bg);
        return;
    }
    if (ch>='0'&&ch<='9') g=font5x7[(ch-'0')+(48-32)];
    else if (ch==':')     g=font5x7[(58-32)];
    else { char up=(ch>='a'&&ch<='z')?(ch-32):ch;
           if (up>='A'&&up<='Z') g=font5x7[(up-'A')+(65-32)]; }
    if (!g){
        for(int dx=0; dx<6*scale; ++dx)
            for(int dy=0; dy<7*scale; ++dy)
                putpx(fb, x+dx, y+dy, bg);
        return;
    }
    for (int c=0;c<5;c++){
        uint8_t bits=g[c];
        for (int r=0;r<7;r++){
            uint16_t col=((bits>>r)&1U)?fg:bg;
            for(int sx=0;sx<scale;sx++)
                for(int sy=0;sy<scale;sy++)
                    putpx(fb,x+c*scale+sx,y+r*scale+sy,col);
        }
    }
    for(int sx=0;sx<scale;sx++)
        for(int sy=0;sy<7*scale;sy++)
            putpx(fb,x+5*scale+sx,y+sy,bg);
}
static void draw_text(uint8_t *fb, int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg){
    int cx=x; while(*s){ draw_char(fb,cx,y,*s,scale,fg,bg); cx+=6*scale; s++; }
}

// ---------- COLETORES ----------
enum { CP_USER=0, CP_NICE=1, CP_SYS=2, CP_INTR=3, CP_IDLE=4, CPUSTATES=5 };

static double read_cpu_percent(void){
    long a[CPUSTATES]={0}, b[CPUSTATES]={0};
    size_t len=sizeof(a);
    if (sysctlbyname("kern.cp_time", a, &len, NULL, 0)!=0) return -1.0;
    usleep(250000); // 250ms para delta rápido (menos “pesado” que 1s)
    len=sizeof(b);
    if (sysctlbyname("kern.cp_time", b, &len, NULL, 0)!=0) return -1.0;
    long du=b[CP_USER]-a[CP_USER], dn=b[CP_NICE]-a[CP_NICE],
         ds=b[CP_SYS]-a[CP_SYS],  di=b[CP_INTR]-a[CP_INTR],
         dl=b[CP_IDLE]-a[CP_IDLE];
    long tot=du+dn+ds+di+dl;
    if (tot<=0) return 0.0;
    return 100.0 * (double)(tot - dl) / (double)tot;
}

static double read_mem_percent(void){
    uint64_t total=0, freep=0;
    size_t l=sizeof(uint64_t);
    if (sysctlbyname("vm.stats.vm.v_page_count",&total,&l,NULL,0)!=0) return -1.0;
    if (sysctlbyname("vm.stats.vm.v_free_count",&freep,&l,NULL,0)!=0) return -1.0;
    if (total==0) return -1.0;
    return 100.0 * (double)(total - freep) / (double)total;
}

static double read_disk_percent(const char *path){
    struct statfs s;
    if (statfs(path,&s)!=0) return -1.0;
    double total=(double)s.f_blocks * (double)s.f_bsize;
    double avail=(double)s.f_bavail * (double)s.f_bsize;
    if (total<=0) return -1.0;
    return 100.0 * (1.0 - (avail/total));
}

static double read_temp_c(void){
    // Tenta dev.cpu.0.temperature (coretemp) — costuma vir string "XX.XC"
    char buf[64]={0}; size_t l=sizeof(buf);
    if (sysctlbyname("dev.cpu.0.temperature", buf, &l, NULL, 0)==0){
        char *end=strchr(buf,'C'); if (end) *end='\0';
        return atof(buf);
    }
    // Fallback ACPI: hw.acpi.thermal.tz0.temperature também vem "XX.XC"
    l=sizeof(buf); memset(buf,0,sizeof(buf));
    if (sysctlbyname("hw.acpi.thermal.tz0.temperature", buf, &l, NULL, 0)==0){
        char *end=strchr(buf,'C'); if (end) *end='\0';
        return atof(buf);
    }
    return -1.0;
}

// ---------- USB ----------
struct usb_out { libusb_device_handle *h; int ifnum; unsigned char ep_out; };

static int find_hid_out(libusb_device_handle *h, struct usb_out *o){
    libusb_device *dev=libusb_get_device(h);
    struct libusb_config_descriptor *cfg=NULL;
    if (libusb_get_active_config_descriptor(dev,&cfg)!=0) return -1;
    int ok=-1;
    for(int i=0;i<cfg->bNumInterfaces && ok==-1;i++){
        const struct libusb_interface *itf=&cfg->interface[i];
        for(int a=0;a<itf->num_altsetting && ok==-1;a++){
            const struct libusb_interface_descriptor *alt=&itf->altsetting[a];
            if (alt->bInterfaceClass==0x03){ // HID
                for(int e=0;e<alt->bNumEndpoints;e++){
                    const struct libusb_endpoint_descriptor *ep=&alt->endpoint[e];
                    if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT){
                        o->ifnum=alt->bInterfaceNumber; o->ep_out=ep->bEndpointAddress; ok=0; break;
                    }
                }
            }
        }
    }
    if (cfg) libusb_free_config_descriptor(cfg);
    return ok;
}
static int xfer(libusb_device_handle *h, unsigned char ep, const void *buf, int len){
    int x=0,r=libusb_interrupt_transfer(h,ep,(unsigned char*)buf,len,&x,1000);
    return (r==0 && x==len)?0:-1;
}
static int send_packet(struct usb_out *o, const uint8_t hdr[8], const uint8_t *data, int datalen){
    static uint8_t buf[8+CHUNK_DATA];
    if (datalen>CHUNK_DATA) datalen=CHUNK_DATA;
    memcpy(buf,hdr,8);
    if (datalen>0) memcpy(buf+8,data,datalen);
    if (datalen<CHUNK_DATA) memset(buf+8+datalen,0,CHUNK_DATA-datalen);
    return xfer(o->h,o->ep_out,buf,8+CHUNK_DATA);
}
static int cmd_orientation(struct usb_out *o, uint8_t mode){ // 0x01 = landscape
    uint8_t hdr[8]={0x55,0xA1,0xF1,mode,0,0,0,0}; return send_packet(o,hdr,NULL,0);
}
static int cmd_time(struct usb_out *o, const struct tm *tmn){
    uint8_t hdr[8]={0x55,0xA1,0xF2,(uint8_t)tmn->tm_hour,(uint8_t)tmn->tm_min,(uint8_t)tmn->tm_sec,0,0};
    return send_packet(o,hdr,NULL,0);
}
static int cmd_redraw_27(struct usb_out *o, const uint8_t *fb){
    size_t off=0;
    for(int i=0;i<TOTAL_CHUNKS;i++){
        int payload=(i<CHUNKS_FULL)?CHUNK_DATA:CHUNK_LAST;
        uint8_t seq=0x01+i;
        uint8_t cmd=(i==0)?0xF0:((i==TOTAL_CHUNKS-1)?0xF2:0xF1);
        uint8_t hdr[8]={0x55,0xA3,cmd,seq,0,0,0,0};
        if (send_packet(o,hdr,fb+off,payload)!=0) return -1;
        off+=payload;
    }
    return 0;
}

// ---------- MAIN ----------
int main(void){
    // 1) Coletar métricas
    double cpu = read_cpu_percent();
    double mem = read_mem_percent();
    double dsk = read_disk_percent("/");
    double tmp = read_temp_c();

    char s_cpu[32], s_mem[32], s_dsk[32], s_tmp[32], s_clk[16];
    snprintf(s_cpu,sizeof(s_cpu),"CPU  %2.0f%%", (cpu<0?0:cpu));
    snprintf(s_mem,sizeof(s_mem),"MEM  %2.0f%%", (mem<0?0:mem));
    snprintf(s_dsk,sizeof(s_dsk),"DISK %2.0f%%", (dsk<0?0:dsk));
    if (tmp>=-0.5) snprintf(s_tmp,sizeof(s_tmp),"TEMP %2.0fC", tmp);
    else           snprintf(s_tmp,sizeof(s_tmp),"TEMP --");

    // Relógio pequeno no topo direito
    time_t now=time(NULL); struct tm tmn; localtime_r(&now,&tmn);
    snprintf(s_clk,sizeof(s_clk),"%02d:%02d",tmn.tm_hour, tmn.tm_min);

    // 2) Desenhar framebuffer (layout limpo)
    static uint8_t fb[FB_SIZE];
    const uint16_t BG  = RGB565(0,0,0);
    const uint16_t FG  = RGB565(230,230,230);   // texto principal
    const uint16_t SUB = RGB565(120,160,200);   // título “System”
    fb_clear(fb,BG);

    // Cabeçalho
    draw_text(fb, 10, 10, "System", 2, SUB, BG);
    draw_text(fb, LCD_W-48, 10, s_clk, 2, SUB, BG);

    // Quatro linhas grandes
    draw_text(fb, 10, 50,  s_cpu, 3, FG, BG);
    draw_text(fb, 10, 85,  s_mem, 3, FG, BG);
    draw_text(fb, 10, 120, s_dsk, 3, FG, BG);
    draw_text(fb, 10, 155, s_tmp, 3, FG, BG);

    // 3) USB → painel
    libusb_context *ctx=NULL;
    if (libusb_init(&ctx)!=0) errx(1,"libusb_init");
    libusb_device_handle *h=libusb_open_device_with_vid_pid(ctx,VID,PID);
    if (!h) errx(1,"nao encontrou 04D9:FD01");
    struct usb_out o={.h=h,.ifnum=-1,.ep_out=0};
    if (find_hid_out(h,&o)!=0) errx(1,"endpoint OUT HID nao encontrado");
    if (libusb_claim_interface(h,o.ifnum)!=0) errx(1,"claim interface");

    // Ordem que derruba o banner: orientação -> heartbeat -> redraw
    if (cmd_orientation(&o,0x01)!=0) warnx("orientation");
    if (cmd_time(&o,&tmn)!=0)        warnx("time");
    if (cmd_redraw_27(&o,fb)!=0)     warnx("redraw");

    libusb_release_interface(h,o.ifnum);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
