// s1panel_freebsd.c  —  utilitário minimalista p/ ACEMAGIC S1 no FreeBSD
// Compilar:  cc -O2 -Wall -I/usr/local/include -L/usr/local/lib -lusb -o /usr/local/bin/s1panel_freebsd s1panel_freebsd.c
// Refs de protocolo e formato: README do projeto AceMagic-S1-LED-TFT-Linux (comandos 0xA1/0xA2/0xA3; 320x170 RGB565; blocos 4096). 
// https://github.com/tjaworski/AceMagic-S1-LED-TFT-Linux  (seção "commands for TFT screen" e "Image Data")

#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <err.h>
#include <libusb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define VID 0x04d9
#define PID 0xfd01

// Dimensões do LCD (documentadas): 320x170, RGB565
#define LCD_W 320
#define LCD_H 170
#define BYTES_PER_PIXEL 2
#define FB_SIZE (LCD_W*LCD_H*BYTES_PER_PIXEL)

// Pacotes: 8 bytes de cabeçalho + até 4096 de dados
#define CHUNK_DATA 4096
#define CHUNK_TOTAL (8 + CHUNK_DATA)

// ---- Fonte 5x7 (ASCII básico 32..90) ----
static const uint8_t font5x7[][7] = { // apenas caracteres necessários; espaço (32) até 'Z'(90)
 // cada byte = 1 coluna (bit 0=topo), 5 colunas úteis; 7 linhas
 // Espaço (32)
 {0x00,0x00,0x00,0x00,0x00},
 // '!'(33) ... (omitimos alguns não usados para encurtar)
 // '0'(48)
 {0x3E,0x51,0x49,0x45,0x3E}, // 0
 {0x00,0x42,0x7F,0x40,0x00}, // 1
 {0x42,0x61,0x51,0x49,0x46}, // 2
 {0x21,0x41,0x45,0x4B,0x31}, // 3
 {0x18,0x14,0x12,0x7F,0x10}, // 4
 {0x27,0x45,0x45,0x45,0x39}, // 5
 {0x3C,0x4A,0x49,0x49,0x30}, // 6
 {0x01,0x71,0x09,0x05,0x03}, // 7
 {0x36,0x49,0x49,0x49,0x36}, // 8
 {0x06,0x49,0x49,0x29,0x1E}, // 9
 // ':'(58)
 {0x00,0x36,0x36,0x00,0x00},
 // ' '(32) a '9'(57) coberto; agora letras usadas L,O,A,D,U,P,T,I,M,E
 // 'A'(65)
 {0x7E,0x11,0x11,0x11,0x7E}, // A
 {0x7F,0x49,0x49,0x49,0x36}, // B
 {0x3E,0x41,0x41,0x41,0x22}, // C
 {0x7F,0x41,0x41,0x22,0x1C}, // D
 {0x7F,0x49,0x49,0x49,0x41}, // E
 {0x7F,0x09,0x09,0x09,0x01}, // F
 {0x3E,0x41,0x49,0x49,0x7A}, // G
 {0x7F,0x08,0x08,0x08,0x7F}, // H
 {0x00,0x41,0x7F,0x41,0x00}, // I
 {0x20,0x40,0x41,0x3F,0x01}, // J
 {0x7F,0x08,0x14,0x22,0x41}, // K
 {0x7F,0x40,0x40,0x40,0x40}, // L
 {0x7F,0x02,0x0C,0x02,0x7F}, // M
 {0x7F,0x04,0x08,0x10,0x7F}, // N
 {0x3E,0x41,0x41,0x41,0x3E}, // O
 {0x7F,0x09,0x09,0x09,0x06}, // P
 {0x3E,0x41,0x51,0x21,0x5E}, // Q
 {0x7F,0x09,0x19,0x29,0x46}, // R
 {0x46,0x49,0x49,0x49,0x31}, // S
 {0x01,0x01,0x7F,0x01,0x01}, // T
 {0x3F,0x40,0x40,0x40,0x3F}, // U
 {0x1F,0x20,0x40,0x20,0x1F}, // V
 {0x7F,0x20,0x18,0x20,0x7F}, // W
 {0x63,0x14,0x08,0x14,0x63}, // X
 {0x07,0x08,0x70,0x08,0x07}, // Y
 {0x61,0x51,0x49,0x45,0x43}  // Z
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b){
    return (uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}
static inline uint16_t swap16(uint16_t v){ return (uint16_t)((v>>8) | (v<<8)); }

static void fb_clear(uint8_t *fb, uint16_t color){
    uint16_t c = swap16(color);
    for (size_t i=0;i<FB_SIZE;i+=2){
        fb[i]   = (uint8_t)(c & 0xFF);
        fb[i+1] = (uint8_t)(c >> 8);
    }
}

// Desenha um pixel em (x,y)
static inline void putpx(uint8_t *fb, int x, int y, uint16_t color){
    if (x<0||x>=LCD_W||y<0||y>=LCD_H) return;
    size_t off = (y*LCD_W + x)*2;
    uint16_t c = swap16(color);
    fb[off]   = (uint8_t)(c & 0xFF);
    fb[off+1] = (uint8_t)(c >> 8);
}

// Desenha caractere 5x7 (escala integer scale)
static void draw_char(uint8_t *fb, int x, int y, char ch, int scale, uint16_t fg, uint16_t bg){
    uint8_t pattern[5]={0};
    if (ch==' ') { // espaço
        // bloco vazio de 5 col + 1 col de espaçamento
        for(int dx=0; dx<6*scale; ++dx)
            for(int dy=0; dy<7*scale; ++dy)
                putpx(fb, x+dx, y+dy, bg);
        return;
    }
    // mapear para nossa tabela (somente subset)
    const uint8_t *glyph=NULL;
    if (ch>='0' && ch<='9')        glyph = font5x7[(ch - '0') + (48-32)];
    else if (ch==':')              glyph = font5x7[(58 - 32)];
    else {
        char up = (ch>='a'&&ch<='z') ? (ch-32) : ch;
        if (up>='A' && up<='Z')    glyph = font5x7[(up - 'A') + (65-32)];
    }
    // se não suportado, desenha espaço
    if (!glyph){ 
        for(int dx=0; dx<6*scale; ++dx)
            for(int dy=0; dy<7*scale; ++dy)
                putpx(fb, x+dx, y+dy, bg);
        return;
    }
    for (int col=0; col<5; ++col){
        uint8_t bits = glyph[col];
        for (int row=0; row<7; ++row){
            bool on = (bits >> row) & 1U;
            uint16_t c = on ? fg : bg;
            // aplicar escala
            for (int sx=0; sx<scale; ++sx)
                for (int sy=0; sy<scale; ++sy)
                    putpx(fb, x + col*scale + sx, y + row*scale + sy, c);
        }
    }
    // coluna de espaçamento
    for (int sx=0; sx<scale; ++sx)
        for (int sy=0; sy<7*scale; ++sy)
            putpx(fb, x + 5*scale + sx, y + sy, bg);
}

static void draw_text(uint8_t *fb, int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg){
    int cx = x;
    while (*s){
        draw_char(fb, cx, y, *s, scale, fg, bg);
        cx += (6*scale);
        ++s;
    }
}

// --------- Coletas (uptime, load) ----------
static void get_uptime(char *buf, size_t bufsz){
    struct timespec boottime;
    size_t len = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) == 0){
        time_t now = time(NULL);
        time_t up = now - boottime.tv_sec;
        int days = (int)(up / 86400);
        int hrs  = (int)((up % 86400) / 3600);
        int mins = (int)((up % 3600) / 60);
        snprintf(buf, bufsz, "UPTIME %dd %02d:%02d", days, hrs, mins);
    } else {
        strlcpy(buf, "UPTIME ?", bufsz);
    }
}

static void get_load(char *buf, size_t bufsz){
    double l[3];
    if (getloadavg(l,3) >= 0)
        snprintf(buf, bufsz, "LOAD %.2f %.2f %.2f", l[0], l[1], l[2]);
    else
        strlcpy(buf, "LOAD ?", bufsz);
}

// --------- USB helpers ----------
struct usb_out {
    libusb_device_handle *h;
    int interface_number;
    unsigned char ep_out; // endpoint OUT (interrupt/bulk)
};

static int find_hid_out_ep(libusb_device_handle *h, struct usb_out *out){
    libusb_device *dev = libusb_get_device(h);
    struct libusb_device_descriptor dd;
    if (libusb_get_device_descriptor(dev, &dd) != 0) return -1;

    struct libusb_config_descriptor *cfg = NULL;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0) return -1;

    int found = -1;
    for (int i=0;i<cfg->bNumInterfaces && found==-1;i++){
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int a=0;a<itf->num_altsetting && found==-1;a++){
            const struct libusb_interface_descriptor *alt = &itf->altsetting[a];
            if (alt->bInterfaceClass == 0x03) { // HID
                // procurar endpoint OUT
                for (int e=0; e<alt->bNumEndpoints; e++){
                    const struct libusb_endpoint_descriptor *ep = &alt->endpoint[e];
                    if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT){
                        out->interface_number = alt->bInterfaceNumber;
                        out->ep_out = ep->bEndpointAddress;
                        found = 0;
                        break;
                    }
                }
            }
        }
    }
    if (cfg) libusb_free_config_descriptor(cfg);
    return found;
}

// Envia um pacote (8 header + até 4096 bytes de dados)
static int send_packet(struct usb_out *U, const uint8_t header[8], const uint8_t *data, int datalen){
    uint8_t buf[CHUNK_TOTAL];
    int xfer=0, r=0;

    if (datalen > CHUNK_DATA) datalen = CHUNK_DATA;

    memcpy(buf, header, 8);
    if (datalen>0) memcpy(buf+8, data, datalen);
    if (datalen<CHUNK_DATA) memset(buf+8+datalen, 0x00, CHUNK_DATA - datalen);

    // Interrupt/Bulk OUT (o driver HID costuma expor como interrupt)
    r = libusb_interrupt_transfer(U->h, U->ep_out, buf, sizeof(buf), &xfer, 1000);
    if (r != 0 || xfer != (int)sizeof(buf)) return -1;
    return 0;
}

// Comandos conforme README do projeto
static int cmd_set_orientation(struct usb_out *U, uint8_t landscape_or_portrait){
    uint8_t hdr[8] = {0x55, 0xA1, 0xF1, landscape_or_portrait, 0,0,0,0};
    return send_packet(U, hdr, NULL, 0);
}
static int cmd_set_time(struct usb_out *U, const struct tm *tmnow){
    uint8_t hdr[8] = {0x55, 0xA1, 0xF2,
                      (uint8_t)tmnow->tm_hour,
                      (uint8_t)tmnow->tm_min,
                      (uint8_t)tmnow->tm_sec,
                      0x00,0x00};
    return send_packet(U, hdr, NULL, 0);
}

// Envia framebuffer completo (redraw): sequências 0x01..0x1B (ver README)
static int cmd_redraw_full(struct usb_out *U, const uint8_t *fb, size_t fbsize){
    // 320*170*2 = 108,800 bytes => 26 chunks de 4096 (25 cheios + 1 parcial 2304)
    const int total_seq = 0x1B; // 27? README mostra 0x1B terminando com 2304 bytes
    // faremos por cálculo do restante
    size_t sent = 0;
    uint8_t seq = 0x01;

    while (sent < fbsize) {
        size_t left = fbsize - sent;
        int payload = (left >= CHUNK_DATA) ? CHUNK_DATA : (int)left;
        uint8_t cmd = 0xF1; // continue
        if (seq == 0x01) cmd = 0xF0; // start
        // prever fim
        size_t after = sent + payload;
        bool is_last = (after >= fbsize);
        if (is_last) cmd = 0xF2; // end

        // offset/length no header (campos são ignorados no offset a partir de 0x11 segundo README, mas preencheremos)
        uint16_t off = (uint16_t)(sent / 256); // só ilustrativo
        uint16_t len = (uint16_t)((payload)/256);

        uint8_t hdr[8] = {0x55, 0xA3, cmd, seq, (uint8_t)(off & 0xFF), (uint8_t)(off>>8),
                          (uint8_t)(len & 0xFF), (uint8_t)(len>>8)};

        if (send_packet(U, hdr, fb + sent, payload) != 0) return -1;

        sent += payload;
        seq++;
        if (seq > 0x1B) break; // segurança para não correr infinito
    }
    return 0;
}

int main(void){
    // 1) Coletar métricas
    char line1[64], line2[64];
    get_uptime(line1, sizeof(line1));
    get_load(line2, sizeof(line2));

    // 2) Montar framebuffer
    static uint8_t fb[FB_SIZE];
    uint16_t black = rgb565(0,0,0);
    uint16_t white = rgb565(31,63,31); // verde claro (g domina no RGB565)
    uint16_t gray  = rgb565(8,16,8);

    fb_clear(fb, black);
    // título discreto
    draw_text(fb, 6, 10, "UPTIME", 2, white, black);
    draw_text(fb, 6, 40, line1 + 7 /* pula "UPTIME " */, 2, white, black);
    draw_text(fb, 6, 80, "LOAD", 2, white, black);
    draw_text(fb, 6, 110, line2 + 5 /* pula "LOAD " */, 2, white, black);
    // rodapé
    draw_text(fb, 6, 148, "OPNSENSE", 1, gray, black);

    // 3) USB open + enviar comandos
    libusb_context *ctx=NULL;
    if (libusb_init(&ctx) != 0) errx(1, "libusb_init falhou");
    libusb_device_handle *h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) errx(1, "nao encontrou 04D9:FD01 (verifique usbconfig)");
    struct usb_out U = {.h=h, .interface_number=-1, .ep_out=0};

    if (find_hid_out_ep(h, &U) != 0) errx(1, "nao achei endpoint OUT HID");
    if (libusb_claim_interface(h, U.interface_number) != 0) errx(1, "claim interface falhou");

    // orientação: 0x01 landscape, 0x02 portrait (README)
    if (cmd_set_orientation(&U, 0x02) != 0) warnx("set_orientation falhou");

    // heartbeat/set_time (use F2/heartbeat)
    time_t now = time(NULL);
    struct tm tmnow; localtime_r(&now, &tmnow);
    if (cmd_set_time(&U, &tmnow) != 0) warnx("set_time falhou");

    // enviar framebuffer completo (redraw)
    if (cmd_redraw_full(&U, fb, FB_SIZE) != 0) warnx("redraw falhou");

    libusb_release_interface(h, U.interface_number);
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
