/*
 * host_sim.c - PC-Testrahmen: derselbe Decoderkern, Abtastwerte aus einer Datei
 *
 *   ./host_sim datei.raw [-t] [-q] [-b]
 *     -t  zusaetzlich Klartext   -b  Kanal B   -q  keine Statistik am Ende
 *   ./host_sim -n datei.nmea
 *     NMEA-Saetze (!AIVDM) lesen und nur als Klartext ausgeben (Test der Textdekodierung)
 *   Eingabe: uint16 little endian, 10-Bit-Werte, Abtastrate 9600*AIS_OSR
 *   (siehe tools/wav2raw.py; Standard: AIS_OSR = 4 -> 38,4 kHz)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ais_app.h"

void ais_putc(char c) { putchar(c); }

/* NMEA-Datei -> Klartext (Mehrteiler werden zusammengesetzt) */
static int nmea_mode(const char *fn)
{
    FILE *f = fopen(fn, "r");
    char line[256];
    static uint8_t d[200];
    unsigned nbits = 0, i;
    if (!f) { perror(fn); return 1; }
    memset(d, 0, sizeof d);
    while (fgets(line, sizeof line, f)) {
        char *p = line, *fld[8];
        int k = 0, total, num, fill;
        if (line[0] != '!') continue;
        fld[k++] = p;
        while (*p && k < 8) { if (*p == ',') { *p = 0; fld[k++] = p + 1; } p++; }
        if (k < 7) continue;
        total = atoi(fld[1]); num = atoi(fld[2]);
        if (num == 1) { memset(d, 0, sizeof d); nbits = 0; }
        for (p = fld[5]; *p; p++) {
            int v = *p - 48; if (v > 40) v -= 8;
            for (i = 0; i < 6; i++, nbits++) if ((v >> (5 - i)) & 1) d[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7));
        }
        fill = atoi(fld[6]);
        if (num == total) {
            nbits -= (unsigned)fill;
            ais_text_write(d, (uint16_t)nbits);
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint16_t x;
    int i, quiet = 0;
    unsigned long n = 0;

    if (argc >= 3 && !strcmp(argv[1], "-n")) return nmea_mode(argv[2]);
    if (argc < 2) { fprintf(stderr, "Aufruf: %s datei.raw [-t] [-b] [-q]\n", argv[0]); return 1; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    ais_app_init();
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) ais_app_cmd('d');
        else if (!strcmp(argv[i], "-b")) ais_app_cmd('b');
        else if (!strcmp(argv[i], "-q")) quiet = 1;
    }
    while (fread(&x, 2, 1, f) == 1) {
        ais_dsp_sample(x);
        n++;
        ais_app_poll();                  /* wie die Hauptschleife auf dem AVR */
    }
    ais_app_poll();
    if (!quiet) {
        fprintf(stderr, "# %.1f s, ok %u, CRC-Fehler %u, Typ/Laenge %u, Rahmen %u, Abbrueche %u, Ueberlauf %u\n",
                n / (double)AIS_FS, ais_app_stat.ok, ais_app_stat.bad_crc, ais_app_stat.bad_type,
                ais_dsp_stat.frames, ais_dsp_stat.aborts, ais_dsp_stat.overflow);
    }
    fclose(f);
    return 0;
}
