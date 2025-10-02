/*
 * ACEMAGIC S1 (Holtek 04d9:fd01) - FreeBSD system stats to front LCD
 * Protocolo baseado em: https://github.com/tjaworski/AceMagic-S1-LED-TFT-Linux
 * - Pacote sempre 4104 bytes: 8 de header + 4096 dados
 * - Comandos: set_orientation (0xA1 0xF1), set_time/heartbeat (0xA1 0xF2/F3),
 *             redraw (0xA3 F0/F1/F2), update (0xA2) — aqui usamos redraw completo.
 * - Framebuffer: 320x170 RGB565, endian swap no envio. 108800 bytes por frame.
 *
 * Requisitos: FreeBSD 13+/14+, /dev/uhidN acessível (IF#1 do 04d9:fd01).
 */

#define _WITH_GETLINE
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ===================== Config / Const ===================== */

#define UHID_PATH          "/dev/uhid1"   /* ajuste se necessário */
#define WIDTH              320
#define HEIGHT             170
#define BPP                2              /* RGB565 = 2 bytes */
#define FRAME_BYTES        (WIDTH*HEIGHT*BPP) /* 108800 */
#define CHUNK_DATA         4096
#define PKT_SIZE           (8 + CHUNK_DATA)   /* 4104 */
#define REDRAW_BLOCKS      27             /* 26x4096 + 2304 */
#define HEARTBEAT_MS       1000
#define UPDATE_MS          1000

/* Header helpers */
#define SIG_BYTE           0x55

/* Byte-order swap para RGB565 (16-bit) */
static inline uint16_t bswap16(uint16_t v){ return (uint16_t)((v>>8) | (v<<8)); }

/* RGB565 macro (5 bits R, 6 G, 5 B) */
static inline uint16_t rgb565(uint8_t r5, uint8_t g6, uint8_t b5){
    return (uint16_t)(((r5 & 0x1F) << 11) | ((g6 & 0x3F) << 5) | (b5 & 0x1F));
}

/* Conveniência: converte 8-bit por canal (0..255) para 5/6/5 */
static inline uint16_t rgb_u8(uint8_t r, uint8_t g, uint8_t b){
    return rgb565((uint8_t)(r>>3),(uint8_t)(g>>2),(uint8_t)(b>>3));
}

/* ===================== UHID I/O ===================== */

static int lcd_fd = -1;

/* write_all: garante envio integral */
static bool write_all(int fd, const void *buf, size_t len){
    const uint8_t *p = (const uint8_t*)buf;
    while (len > 0){
        ssize_t w = write(fd, p, len);
        if (w < 0){
            if (errno == EINTR) continue;
            perror("write");
            return false;
        }
        p += (size_t)w;
        len -= (size_t)w;
    }
    return true;
}

/* set_orientation: 0x55 0xA1 0xF1 <orient> 00 00 00 00 + padding 4096 zerada */
static bool lcd_set_orientation(uint8_t orientation){
    uint8_t pkt[PKT_SIZE];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = SIG_BYTE;
    pkt[1] = 0xA1;
    pkt[2] = 0xF1;
    pkt[3] = orientation; /* 0x01 landscape, 0x02 portrait */

    return write_all(lcd_fd, pkt, sizeof pkt);
}

/* set_time / heartbeat: 0x55 0xA1 0xF2 <hh> <mm> <ss> 00 00 + padding */
static bool lcd_set_time_or_heartbeat(bool set_time){
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    uint8_t pkt[PKT_SIZE];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = SIG_BYTE;
    pkt[1] = 0xA1;
    pkt[2] = set_time ? 0xF3 : 0xF2;
    pkt[3] = (uint8_t)tm.tm_hour;
    pkt[4] = (uint8_t)tm.tm_min;
    pkt[5] = (uint8_t)tm.tm_sec;

    return write_all(lcd_fd, pkt, sizeof pkt);
}

/* redraw completo (framebuffer 320x170 RGB565 com swap endian) */
static bool lcd_redraw(const uint8_t *frame /* 108800 bytes */){
    /* Envia 26 blocos de 4096 e 1 bloco final de 2304 */
    size_t remaining = FRAME_BYTES;
    size_t sent_off = 0;

    for (int seq = 1; seq <= REDRAW_BLOCKS; seq++){
        uint8_t pkt[PKT_SIZE];
        memset(pkt, 0, sizeof pkt);

        pkt[0] = SIG_BYTE;
        pkt[1] = 0xA3;
        pkt[2] = (seq == 1) ? 0xF0 : (seq == REDRAW_BLOCKS ? 0xF2 : 0xF1);
        pkt[3] = (uint8_t)seq;

        /* offset/length “documentais”; firmware usa ‘sequence’ (README) */
        /* offset em múltiplos de 0x10 = 4096; último bloco usa 2304 (0x0900) */
        uint16_t off16 = (uint16_t)((sent_off / CHUNK_DATA) * 0x10);
        uint16_t len16 = (uint16_t)((remaining >= CHUNK_DATA) ? 0x10 : 0x09); /* 0x10=4096, 0x09=2304 */
        pkt[4] = (uint8_t)(off16 & 0xFF);
        pkt[5] = (uint8_t)((off16 >> 8) & 0xFF);
        pkt[6] = (uint8_t)(len16 & 0xFF);
        pkt[7] = (uint8_t)((len16 >> 8) & 0xFF);

        size_t this_data = (remaining >= CHUNK_DATA) ? CHUNK_DATA : 2304;
        memcpy(pkt + 8, frame + sent_off, this_data);
        /* resto do data já está zerado por memset */

        if (!write_all(lcd_fd, pkt, sizeof pkt)) return false;

        sent_off += this_data;
        if (remaining >= this_data) remaining -= this_data; else remaining = 0;
    }
    return (remaining == 0);
}

/* ===================== Métricas FreeBSD ===================== */

enum { CP_USER = 0, CP_NICE, CP_SYS, CP_INTR, CP_IDLE, CPUSTATES = 5 };

static bool get_cpu_times(uint64_t out_times[CPUSTATES]){
    long raw[CPUSTATES]={0};
    size_t sraw = sizeof(raw);
    if (sysctlbyname("kern.cp_time", &raw, &sraw, NULL, 0) != 0) return false;
    for (int i=0;i<CPUSTATES;i++) out_times[i]=(uint64_t)raw[i];
    return true;
}

static double get_cpu_percent(void){
    static bool inited=false;
    static uint64_t prev[CPUSTATES]={0};
    uint64_t now[CPUSTATES];
    if (!get_cpu_times(now)) return 0.0;

    if (!inited){
        inited=true; memcpy(prev, now, sizeof prev);
        usleep(150000);
        if (!get_cpu_times(now)) return 0.0;
    }
    uint64_t diff[CPUSTATES]={0}, total=0;
    for (int i=0;i<CPUSTATES;i++){ diff[i]=now[i]-prev[i]; total+=diff[i]; prev[i]=now[i]; }
    if (!total) return 0.0;
    double used = 100.0 * (double)(diff[CP_USER]+diff[CP_NICE]+diff[CP_SYS]+diff[CP_INTR]) / (double)total;
    return used;
}

typedef struct { uint64_t total, used, avail; } mem_info_t;
static bool get_mem_info(mem_info_t *mi){
    if (!mi) return false;
    int page_size=0; size_t psz=sizeof(page_size);
    if (sysctlbyname("hw.pagesize",&page_size,&psz,NULL,0)!=0) return false;
    uint64_t v_page_count=0,v_free=0,v_active=0,v_inactive=0,v_cache=0,v_wire=0; size_t sz=sizeof(uint64_t);
    if (sysctlbyname("vm.stats.vm.v_page_count",&v_page_count,&sz,NULL,0)!=0) return false;
    if (sysctlbyname("vm.stats.vm.v_free_count",&v_free,&sz,NULL,0)!=0) return false;
    if (sysctlbyname("vm.stats.vm.v_active_count",&v_active,&sz,NULL,0)!=0) return false;
    if (sysctlbyname("vm.stats.vm.v_inactive_count",&v_inactive,&sz,NULL,0)!=0) return false;
    if (sysctlbyname("vm.stats.vm.v_cache_count",&v_cache,&sz,NULL,0)!=0) return false;
    if (sysctlbyname("vm.stats.vm.v_wire_count",&v_wire,&sz,NULL,0)!=0) return false;
    mi->total = v_page_count * (uint64_t)page_size;
    mi->used  = (v_active + v_wire) * (uint64_t)page_size;
    mi->avail = (v_free + v_inactive + v_cache) * (uint64_t)page_size;
    return true;
}

typedef struct { uint64_t total, used, avail; } disk_info_t;
static bool get_disk_root(disk_info_t *di){
    struct statfs sfs;
    if (statfs("/", &sfs)!=0) return false;
    uint64_t bsize=sfs.f_bsize, blocks=sfs.f_blocks, bfree=sfs.f_bfree, bavail=sfs.f_bavail;
    di->total = blocks*bsize;
    di->used  = (blocks-bfree)*bsize;
    di->avail = bavail*bsize;
    return true;
}

static double read_temp_c(void){
    /* Tenta chaves comuns */
    const char *keys[] = {"dev.cpu.0.temperature","hw.acpi.thermal.tz0.temperature","hw.acpi.thermal.tz1.temperature",NULL};
    for (int i=0; keys[i]; i++){
        double d; size_t sz=sizeof(d);
        if (sysctlbyname(keys[i], &d, &sz, NULL, 0)==0) return d; /* geralmente já vem em °C */
        /* tenta como string */
        char buf[64]; sz=sizeof(buf);
        if (sysctlbyname(keys[i], &buf, &sz, NULL, 0)==0){
            char *end=NULL; double v=strtod(buf,&end);
            if (end && *end=='K') return v-273.15;
            return v;
        }
    }
    return -273.15;
}

/* ===================== Renderização (fonte 5x7) ===================== */

/* Fonte 5x7 minimalista para letras usadas + dígitos (ASCII parcial)
 * Cada char: 5 colunas x 7 linhas, 1 bit por pixel em cada coluna (LSB = topo)
 * Ajuste/adicione se quiser mais caracteres.
 */
typedef struct { char ch; uint8_t col[5]; } glyph_t;

/* Dígitos 0-9, símbolos básicos e letras necessárias (C,P,U,R,A,M,D,I,S,K,T,E,F,/,%,:,.,space) */
static const glyph_t FONT[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {':', {0x00,0x14,0x00,0x14,0x00}},
    {'.', {0x00,0x00,0x00,0x10,0x00}},
    {'/', {0x01,0x02,0x04,0x08,0x10}},
    {'%', {0x61,0x62,0x04,0x19,0x31}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x62,0x51,0x49,0x49,0x46}},
    {'3', {0x22,0x41,0x49,0x49,0x36}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3E,0x49,0x49,0x49,0x32}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x26,0x49,0x49,0x49,0x3E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x26,0x49,0x49,0x49,0x32}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}},
};

static const glyph_t* find_glyph(char c){
    size_t n = sizeof(FONT)/sizeof(FONT[0]);
    for (size_t i=0;i<n;i++) if (FONT[i].ch==c) return &FONT[i];
    return &FONT[0]; /* space fallback */
}

static void put_pixel(uint8_t *fb, int x, int y, uint16_t color){
    if (x<0 || x>=WIDTH || y<0 || y>=HEIGHT) return;
    size_t idx = (size_t)((y*WIDTH + x) * 2);
    /* endian swap exigido pelo dispositivo */
    *(uint16_t*)(fb + idx) = bswap16(color);
}

/* Desenha char 5x7, com espaçamento 1px, altura 7 */
static void draw_char(uint8_t *fb, int x, int y, char c, uint16_t fg, uint16_t bg){
    const glyph_t *g = find_glyph(c);
    for (int col=0; col<5; col++){
        uint8_t bits = g->col[col];
        for (int row=0; row<7; row++){
            bool on = (bits >> row) & 0x1;
            put_pixel(fb, x+col, y+row, on?fg:bg);
        }
    }
    /* coluna de espaçamento */
    for (int row=0; row<7; row++) put_pixel(fb, x+5, y+row, bg);
}

/* Desenha string */
static void draw_text(uint8_t *fb, int x, int y, const char *s, uint16_t fg, uint16_t bg){
    int cx = x;
    for (; *s; s++){
        draw_char(fb, cx, y, *s, fg, bg);
        cx += 6;
    }
}

/* Retângulo cheio */
static void fill_rect(uint8_t *fb, int x, int y, int w, int h, uint16_t color){
    if (w<=0||h<=0) return;
    int x2 = x+w, y2 = y+h;
    if (x<0) x=0; if (y<0) y=0; if (x2>WIDTH) x2=WIDTH; if (y2>HEIGHT) y2=HEIGHT;
    for (int yy=y; yy<y2; yy++){
        size_t off = (size_t)((yy*WIDTH + x)*2);
        for (int xx=x; xx<x2; xx++){
            *(uint16_t*)(fb+off) = bswap16(color);
            off += 2;
        }
    }
}

/* Barra horizontal  (0..100) */
static void draw_bar(uint8_t *fb, int x, int y, int w, int h, int pct, uint16_t fg, uint16_t bg, uint16_t frame){
    fill_rect(fb, x, y, w, h, bg);
    int fill = (pct<0?0:(pct>100?100:pct)) * w / 100;
    fill_rect(fb, x, y, fill, h, fg);
    /* moldura fina */
    for (int xx=x; xx<x+w; xx++){ put_pixel(fb, xx, y, frame); put_pixel(fb, xx, y+h-1, frame); }
    for (int yy=y; yy<y+h; yy++){ put_pixel(fb, x, yy, frame); put_pixel(fb, x+w-1, yy, frame); }
}

/* ===================== Helpers ===================== */

static void human_bytes(char *out, size_t outsz, uint64_t b){
    const char *u[]={"B","KiB","MiB","GiB","TiB"};
    double v=(double)b; int i=0; while (v>=1024.0 && i<4){ v/=1024.0; i++; }
    snprintf(out,outsz,"%.1f %s",v,u[i]);
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int s){ (void)s; g_stop=1; }

/* ===================== Main render loop ===================== */

int main(void){
    signal(SIGINT, on_sigint);
    signal(SIGTERM,on_sigint);

    lcd_fd = open(UHID_PATH, O_WRONLY);
    if (lcd_fd < 0){
        perror("open /dev/uhidN (use o N correto; precisa permissão/root)");
        return 1;
    }

    /* Inicial: orientação + acertar horário + 1º heartbeat */
    if (!lcd_set_orientation(0x01)){ fprintf(stderr,"set_orientation falhou\n"); }
    if (!lcd_set_time_or_heartbeat(true)){ fprintf(stderr,"set_time falhou\n"); }
    if (!lcd_set_time_or_heartbeat(false)){ fprintf(stderr,"heartbeat falhou\n"); }

    uint8_t *frame = (uint8_t*)calloc(FRAME_BYTES,1);
    if (!frame){ fprintf(stderr,"sem memória\n"); return 1; }

    struct timespec ts_last_hb={0}, ts_now={0};
    clock_gettime(CLOCK_MONOTONIC, &ts_last_hb);

    while (!g_stop){
        /* ===== Coleta métricas ===== */
        double cpu = get_cpu_percent();
        mem_info_t mi; get_mem_info(&mi);
        disk_info_t di; get_disk_root(&di);
        double tc = read_temp_c();

        /* ===== Desenha ===== */
        /* fundo */
        fill_rect(frame, 0, 0, WIDTH, HEIGHT, rgb_u8(0,0,0));

        /* Título */
        draw_text(frame, 8, 8, "S1 STATS", rgb_u8(255,255,255), rgb_u8(0,0,0));

        /* CPU */
        char buf[64];
        snprintf(buf,sizeof buf,"CPU %3.0f%%", cpu);
        draw_text(frame, 8, 28, "CPU", rgb_u8(180,220,255), rgb_u8(0,0,0));
        draw_bar(frame, 60, 28, 240, 10, (int)(cpu+0.5), rgb_u8(80,200,255), rgb_u8(20,40,60), rgb_u8(120,160,200));
        draw_text(frame, 8, 42, buf, rgb_u8(220,220,220), rgb_u8(0,0,0));

        /* RAM */
        char t[32], u[32], a[32];
        human_bytes(t,sizeof t, mi.total);
        human_bytes(u,sizeof u, mi.used);
        human_bytes(a,sizeof a, mi.avail);
        int ram_pct = (mi.total? (int)((mi.used*100.0)/mi.total+0.5):0);
        draw_text(frame, 8, 60, "RAM", rgb_u8(180,255,180), rgb_u8(0,0,0));
        draw_bar(frame, 60, 60, 240, 10, ram_pct, rgb_u8(120,255,120), rgb_u8(16,40,16), rgb_u8(80,140,80));
        snprintf(buf,sizeof buf,"%s used / %s", u, t);
        draw_text(frame, 8, 74, buf, rgb_u8(220,220,220), rgb_u8(0,0,0));
        snprintf(buf,sizeof buf,"%s avail", a);
        draw_text(frame, 8, 86, buf, rgb_u8(170,200,170), rgb_u8(0,0,0));

        /* DISK / */
        human_bytes(t,sizeof t, di.total);
        human_bytes(u,sizeof u, di.used);
        human_bytes(a,sizeof a, di.avail);
        int disk_pct = (di.total? (int)((di.used*100.0)/di.total+0.5):0);
        draw_text(frame, 8, 104, "DISK", rgb_u8(255,220,180), rgb_u8(0,0,0));
        draw_bar(frame, 60, 104, 240, 10, disk_pct, rgb_u8(255,200,120), rgb_u8(40,28,16), rgb_u8(180,140,90));
        snprintf(buf,sizeof buf,"%s used / %s", u, t);
        draw_text(frame, 8, 118, buf, rgb_u8(220,220,220), rgb_u8(0,0,0));
        snprintf(buf,sizeof buf,"%s free", a);
        draw_text(frame, 8, 130, buf, rgb_u8(200,180,150), rgb_u8(0,0,0));

        /* TEMP */
        draw_text(frame, 8, 148, "TEMP", rgb_u8(255,180,180), rgb_u8(0,0,0));
        if (tc > -200.0) {
            snprintf(buf,sizeof buf,"CPU %.1f C", tc);
        } else {
            snprintf(buf,sizeof buf,"CPU N/A");
        }
        draw_text(frame, 60, 148, buf, rgb_u8(255,200,200), rgb_u8(0,0,0));

        /* ===== Envio ===== */
        if (!lcd_redraw(frame)){
            fprintf(stderr,"lcd_redraw falhou\n");
        }

        /* Heartbeat a cada ~1s (README recomenda 1s para não “travar”) */
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        long ms = (ts_now.tv_sec - ts_last_hb.tv_sec)*1000L +
                  (ts_now.tv_nsec - ts_last_hb.tv_nsec)/1000000L;
        if (ms >= HEARTBEAT_MS){
            if (!lcd_set_time_or_heartbeat(false)){
                fprintf(stderr,"heartbeat falhou\n");
            }
            ts_last_hb = ts_now;
        }

        /* Ritmo de atualização das métricas / tela */
        usleep(UPDATE_MS*1000);
    }

    free(frame);
    close(lcd_fd);
    return 0;
}
