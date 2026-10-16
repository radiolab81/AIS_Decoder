/*
 * ais_app.c - Rahmen aus der Warteschlange holen, pruefen, ausgeben
 */
#include "ais_app.h"

#ifdef __AVR__
#define S(x) pstr(PSTR(x))
#else
#define PSTR(x) (x)
#define S(x) pstr(x)
#endif

ais_app_stat_t ais_app_stat;
static uint8_t opt_nmea, opt_text, seq;
static char    chan;
static uint8_t  last_valid;           /* letzte gueltige Nachricht fuer die Doppeltreffer-Unterdrueckung */
static uint16_t last_n, last_fcs, last_tick;

static void pstr(const char *p)
{
    char c;
    while ((c = (char)ais_rom_u8(p++)) != 0) ais_putc(c);
}

static void put_u(uint16_t v)
{
    char b[5]; uint8_t i = 0;
    do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) ais_putc(b[--i]);
}

void ais_app_init(void)
{
    ais_dsp_init();
    opt_nmea = AIS_DEFAULT_NMEA;
    opt_text = AIS_DEFAULT_TEXT;
    chan = AIS_DEFAULT_CHAN;
    seq = 0; last_valid = 0;
    ais_app_stat.ok = ais_app_stat.bad_crc = ais_app_stat.bad_type = ais_app_stat.dup = 0;
}

uint8_t ais_app_poll(void)
{
    uint8_t cnt = 0;
    const ais_frame_t *f;

    while ((f = ais_frame_peek()) != 0) {
        uint8_t err;
        uint16_t n = ais_frame_check(f, &err);
        if (n && last_valid && n == last_n
            && (uint16_t)(f->tick - last_tick) < 4 * AIS_OSR
            && (uint16_t)(f->d[n >> 3] | (f->d[(n >> 3) + 1] << 8)) == last_fcs) {
            /* derselbe Burst, von einer anderen Bitphase ebenfalls fehlerfrei empfangen */
            ais_app_stat.dup++;
        } else if (n) {
            last_valid = 1; last_n = n; last_tick = f->tick;
            last_fcs = (uint16_t)(f->d[n >> 3] | (f->d[(n >> 3) + 1] << 8));
            ais_app_stat.ok++;
            cnt++;
            if (opt_nmea) {
                uint8_t parts = ais_nmea_write(f->d, n, chan, seq);
                if (parts > 1 && ++seq > 9) seq = 0;
            }
            if (opt_text) {
                ais_putc('['); ais_putc(chan); S("] Pegel "); put_u(f->level >> 4); S("  ");
                ais_text_write(f->d, n);
            }
        } else if (err == AIS_ERR_CRC) {
            ais_app_stat.bad_crc++;
        } else if (err != AIS_ERR_SHORT) {
            ais_app_stat.bad_type++;
        }
        ais_frame_release();
    }
    return cnt;
}

void ais_app_cmd(char c)
{
    switch (c) {
    case 'n': opt_nmea ^= 1; break;
    case 'd': opt_text ^= 1; break;
    case 'a': case 'A': chan = 'A'; break;
    case 'b': case 'B': chan = 'B'; break;
    case 's':
        S("# ok "); put_u(ais_app_stat.ok);
        S("  CRC-Fehler "); put_u(ais_app_stat.bad_crc);
        S("  Typ/Laenge "); put_u(ais_app_stat.bad_type);
        S("  Rahmen "); put_u(ais_dsp_stat.frames);
        S("  Abbrueche "); put_u(ais_dsp_stat.aborts);
        S("  Warteschl.-Ueberlauf "); put_u(ais_dsp_stat.overflow);
        S("\r\n");
        break;
    case '?':
        S("# n=NMEA d=Klartext a/b=Kanal s=Statistik\r\n");
        break;
    default: break;
    }
}
