/*
 * ais_msg.c - CRC, NMEA 0183 (!AIVDM) und Klartext-Dekodierung nach ITU-R M.1371-5
 * ================================================================================
 *
 * BITREIHENFOLGE (die Stolperfalle bei AIS)
 * -----------------------------------------
 * Auf der Luftschnittstelle geht in jedem Oktett das niederwertigste Bit zuerst raus (HDLC).
 * Der Sammler in ais_dsp.c speichert die Bits in dieser Reihenfolge (erstes Bit = Bit 0 des
 * ersten Bytes).  Die Datenfelder der Nachricht (Typ, MMSI, ...) sind dagegen "big endian"
 * in der Bitfolge angeordnet, und die NMEA-Nutzdaten (6-Bit-ASCII-Armor) bilden diese Folge
 * ab - also je Byte MSB zuerst.  Daraus folgt:
 *
 *        NMEA-Bit i  =  Bit (7 - (i mod 8))  von Byte (i div 8)
 *
 * Beispiel: erstes Byte der Luft-Bits 1,1,1,0,0,0,0,0 -> Byte 0x07 -> NMEA-Bits 00000111
 * -> Typ = 000001 = 1, Repeat = 11 = 3.
 *
 * Die CRC dagegen laeuft ueber die Bits in LUFT-Reihenfolge (CRC-16/X.25, reflektiert,
 * Polynom 0x8408, Startwert 0xFFFF, Pruefrest 0xF0B8). *
 * NACHRICHTENTYPEN (ITU-R M.1371-5), alle 1..27 werden als Klartext ausgegeben
 * ------------------------------------------------------------------------------
 *   1,2,3  Positionsmeldung Klasse A      14  Sicherheitstext (Broadcast)
 *   4      Basisstation (UTC, Position)   15  Abfrage (Interrogation)
 *   5      Statische/Reisedaten Klasse A  16  Zuweisung (Assignment)
 *   6      Adressierte Binaernachricht    17  DGNSS-Korrekturdaten (RTCM)
 *   7      Quittung binaer                18  Positionsmeldung Klasse B (CS)
 *   8      Binaer-Broadcast               19  Positionsmeldung Klasse B (erweitert)
 *   9      SAR-Luftfahrzeug               20  Datalink-Verwaltung (Slotreservierung)
 *   10     UTC/Datum-Abfrage              21  Seezeichen (AtoN)
 *   11     UTC/Datum-Antwort              22  Kanalverwaltung
 *   12     Adressierter Sicherheitstext   23  Gruppenzuweisung
 *   13     Quittung Sicherheitstext       24  Klasse-B-Statik (Teil A Name, Teil B Daten)
 *                                         25  Einzelslot-Binaer   26  Mehrslot-Binaer
 *                                         27  Langstreckenposition (Satellit)
 *
 * Jede Nachricht ist eine Folge von Feldern fester Bitbreite (z. B. Typ 1: Typ 6 Bit, Repeat 2,
 * MMSI 30, Status 4, Drehrate 8, SOG 10, Genauigkeit 1, Laenge 28, Breite 27, COG 12, HDG 9, ...).
 * ais_get(d, n, pos, len) holt ein solches Feld; die Texte benutzen 6-Bit-ASCII.
 */
#include "ais_msg.h"

#ifdef __AVR__
#define S(x) pstr(PSTR(x))
#define NTH(list, n) put_nth(PSTR(list), (n))
#define SA(c, a, b) pstr((c) ? PSTR(a) : PSTR(b))
#else
#define PSTR(x) (x)
#define S(x) pstr(x)
#define NTH(list, n) put_nth((list), (n))
#define SA(c, a, b) pstr((c) ? (a) : (b))
#endif

#define G(p, l)  ais_get(d, n, (p), (l))
#define GS(p, l) ais_gets(d, n, (p), (l))

/* ================================ Bit-Zugriff ========================================= */
uint32_t ais_get(const uint8_t *d, uint16_t nbits, uint16_t pos, uint8_t len)
{
    uint32_t v = 0;
    uint16_t i = pos, e = pos + len;
    for (; i < e; i++) {
        v <<= 1;
        if (i < nbits && ((d[i >> 3] >> (7 - (i & 7))) & 1)) v |= 1;
    }
    return v;
}

static int32_t ais_gets(const uint8_t *d, uint16_t nbits, uint16_t pos, uint8_t len)
{
    uint32_t v = ais_get(d, nbits, pos, len);
    if (len < 32 && (v >> (len - 1))) v |= 0xFFFFFFFFUL << len;
    return (int32_t)v;
}

/* ================================ CRC + Plausibilitaet ================================ */
/* erlaubte Nutzbitlaengen je Typ: min, max (Bit, ohne FCS) */
static const uint16_t len_tab[28][2] AIS_ROM = {
    {0, 0},                      /*  0 unbekannt                         */
    {168, 168}, {168, 168}, {168, 168},   /*  1..3                      */
    {168, 168},                  /*  4 Basisstation                       */
    {420, 424},                  /*  5                                    */
    {72, 1008},                  /*  6                                    */
    {72, 168},                   /*  7  Quittung (1..4 Empfaenger)        */
    {56, 1008},                  /*  8                                    */
    {168, 168},                  /*  9                                    */
    {72, 72},                    /* 10                                    */
    {168, 168},                  /* 11                                    */
    {72, 1008},                  /* 12                                    */
    {72, 168},                   /* 13                                    */
    {40, 1008},                  /* 14                                    */
    {88, 160},                   /* 15                                    */
    {96, 144},                   /* 16                                    */
    {80, 816},                   /* 17                                    */
    {168, 168},                  /* 18                                    */
    {312, 312},                  /* 19                                    */
    {72, 160},                   /* 20                                    */
    {272, 360},                  /* 21                                    */
    {168, 168},                  /* 22                                    */
    {160, 160},                  /* 23                                    */
    {160, 168},                  /* 24                                    */
    {40, 168},                   /* 25                                    */
    {60, 1008},                  /* 26                                    */
    {96, 168},                   /* 27                                    */
};

uint16_t ais_frame_check(const ais_frame_t *f, uint8_t *err)
{
    uint16_t crc = 0xFFFF, i, n = f->nbits;
    uint8_t type;

    if (n < 56) { *err = AIS_ERR_SHORT; return 0; }
    for (i = 0; i < n; i++) {
        uint8_t fb = (uint8_t)((crc ^ (f->d[i >> 3] >> (i & 7))) & 1);
        crc >>= 1;
        if (fb) crc ^= 0x8408;
    }
    if (crc != 0xF0B8) { *err = AIS_ERR_CRC; return 0; }
    n -= 16;                                    /* FCS abziehen */
    type = (uint8_t)(f->d[0] >> 2);             /* oberste 6 Bit des ersten Bytes = Typ    */
#if AIS_STRICT
    if (type == 0 || type > 27) { *err = AIS_ERR_TYPE; return 0; }
    if (n < ais_rom_u16(&len_tab[type][0]) || n > ais_rom_u16(&len_tab[type][1])) {
        *err = AIS_ERR_LEN; return 0;
    }
#endif
    *err = AIS_OK;
    return n;
}

/* ================================ NMEA 0183 =========================================== */
static uint8_t cs;                              /* laufende Pruefsumme eines Satzes */

static void ncs(char c) { cs ^= (uint8_t)c; ais_putc(c); }

static void nstr(const char *p)
{
    char c;
    while ((c = (char)ais_rom_u8(p++)) != 0) ncs(c);
}

static void nhex(uint8_t v)
{
    ais_putc("0123456789ABCDEF"[v >> 4]);
    ais_putc("0123456789ABCDEF"[v & 15]);
}

uint8_t ais_nmea_write(const uint8_t *d, uint16_t nbits, char chan, uint8_t seq)
{
    uint16_t nchar = (uint16_t)((nbits + 5) / 6);
    uint8_t  parts = (uint8_t)((nchar + 59) / 60);
    uint8_t  fill  = (uint8_t)(nchar * 6 - nbits);
    uint8_t  k;
    uint16_t c = 0;

    for (k = 1; k <= parts; k++) {
        uint16_t last = c + 60;
        if (last > nchar) last = nchar;
        cs = 0;
        ais_putc('!');
        nstr(PSTR("AIVDM,"));
        ncs((char)('0' + parts)); ncs(',');
        ncs((char)('0' + k));     ncs(',');
        if (parts > 1) ncs((char)('0' + seq));
        ncs(',');
        ncs(chan);                ncs(',');
        for (; c < last; c++) {
            uint8_t v = (uint8_t)ais_get(d, nbits, (uint16_t)(c * 6), 6);   /* hinter nbits: 0 = Fuellbits */
            v += 48;
            if (v > 87) v += 8;
            ncs((char)v);
        }
        ncs(',');
        ncs((char)('0' + (k == parts ? fill : 0)));
        ais_putc('*');
        nhex(cs);
        ais_putc('\r'); ais_putc('\n');
    }
    return parts;
}

/* ================================ Textausgabe: Hilfen ================================= */
static void pstr(const char *p)
{
    char c;
    while ((c = (char)ais_rom_u8(p++)) != 0) ais_putc(c);
}

/* n-ten Eintrag einer mit '|' getrennten ROM-Liste ausgeben */
static void put_nth(const char *p, uint8_t n)
{
    char c;
    while (n) {
        c = (char)ais_rom_u8(p++);
        if (c == 0) return;
        if (c == '|') n--;
    }
    while ((c = (char)ais_rom_u8(p++)) != 0 && c != '|') ais_putc(c);
}

static void put_u(uint32_t v)
{
    char b[10]; uint8_t i = 0;
    do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) ais_putc(b[--i]);
}

static void put_pad(uint32_t v, uint8_t w)       /* mit fuehrenden Nullen */
{
    char b[10]; uint8_t i;
    for (i = 0; i < w; i++) { b[i] = (char)('0' + v % 10); v /= 10; }
    while (i) ais_putc(b[--i]);
}

/* Festkomma: v / 10^dec */
static void put_fix(int32_t v, uint8_t dec)
{
    uint32_t u, div = 1;
    uint8_t i;
    if (v < 0) { ais_putc('-'); u = (uint32_t)(-v); } else u = (uint32_t)v;
    for (i = 0; i < dec; i++) div *= 10;
    put_u(u / div);
    if (dec) { ais_putc('.'); put_pad(u % div, dec); }
}

static void sp(uint8_t n) { while (n--) ais_putc(' '); }
static void nl(void)      { ais_putc('\r'); ais_putc('\n'); }
static void ind(void)     { sp(4); }

/* 6-Bit-ASCII-Text; Auffuellung '@' und Leerzeichen am Ende weglassen */
static void put_text(const uint8_t *d, uint16_t n, uint16_t pos, uint8_t nch)
{
    char b[32];
    uint8_t i, len = 0;
    if (nch > sizeof b) nch = sizeof b;
    for (i = 0; i < nch; i++) {
        uint8_t v = (uint8_t)G(pos + i * 6, 6);
        b[i] = (char)(v < 32 ? v + 64 : v);
        if (b[i] != '@' && b[i] != ' ') len = (uint8_t)(i + 1);
    }
    ais_putc('"');
    for (i = 0; i < len; i++) ais_putc(b[i] == '@' ? ' ' : b[i]);
    ais_putc('"');
}

/* langer Text (Typ 12/14/DAC1FI0): Zeichen fuer Zeichen bis zum Ende */
static void put_text_long(const uint8_t *d, uint16_t n, uint16_t pos)
{
    uint16_t i, cnt = 0, e;
    if (pos >= n) { S("\"\""); return; }
    cnt = (uint16_t)((n - pos) / 6);
    e = cnt;
    while (e) {                                 /* Fuellung am Ende abschneiden */
        uint8_t v = (uint8_t)G(pos + (e - 1) * 6, 6);
        if (v != 0 && v != 32) break;
        e--;
    }
    ais_putc('"');
    for (i = 0; i < e; i++) {
        uint8_t v = (uint8_t)G(pos + i * 6, 6);
        ais_putc((char)(v < 32 ? (v == 0 ? ' ' : v + 64) : v));
    }
    ais_putc('"');
}

/* Breite/Laenge: v in 1/10000 Minute */
static void put_coord(int32_t v, uint8_t is_lon)
{
    uint32_t a = (uint32_t)(v < 0 ? -v : v);
    uint32_t deg = a / 600000UL, rem = a % 600000UL;
    put_pad(deg, is_lon ? 3 : 2);
    S("\xC2\xB0");                              /* Gradzeichen UTF-8 */
    put_pad(rem / 10000, 2); ais_putc('.'); put_pad(rem % 10000, 4); ais_putc('\'');
    ais_putc(is_lon ? (v < 0 ? 'W' : 'E') : (v < 0 ? 'S' : 'N'));
}

static void put_pos(int32_t lon, int32_t lat)
{
    S("Pos ");
    if (lat == 91L * 600000L) S("Breite n.v."); else put_coord(lat, 0);
    sp(2);
    if (lon == 181L * 600000L) S("Laenge n.v."); else put_coord(lon, 1);
}

static void put_nv(void) { S("n.v."); }

static void put_mmsi(uint32_t m)
{
    put_pad(m, 9);
    {
        uint32_t a = m / 10000000UL;            /* ersten 2 Ziffern */
        uint32_t t = m / 1000000UL;             /* ersten 3 Ziffern */
        if (m == 0) return;
        sp(1); ais_putc('(');
        if (t == 970)      S("AIS-SART");
        else if (t == 972) S("MOB-Geraet");
        else if (t == 974) S("EPIRB-AIS");
        else if (a == 0)   S("Kuestenfunkstelle");
        else if (a == 99)  S("Seezeichen/AtoN");
        else if (a == 98)  S("Hilfsfahrzeug");
        else if (t / 10 == 11) S("SAR-Luftfahrzeug");
        else if (a < 10 && m / 100000000UL == 0) S("Gruppe");
        else { S("MID "); put_u(t); }
        ais_putc(')');
    }
}

static void put_hexbits(const uint8_t *d, uint16_t n, uint16_t pos, uint16_t end)
{
    uint8_t col = 0;
    if (end > n) end = n;
    if (pos >= end) { S("(keine)"); return; }
    for (; pos < end; pos += 8) {
        uint8_t l = (uint8_t)(end - pos > 8 ? 8 : end - pos);
        uint8_t v = (uint8_t)(ais_get(d, n, pos, l) << (8 - l));
        if (col == 16) { nl(); ind(); sp(4); col = 0; }
        ais_putc("0123456789ABCDEF"[v >> 4]); ais_putc("0123456789ABCDEF"[v & 15]);
        ais_putc(' ');
        col++;
    }
}

static void put_epfd(uint8_t v)
{
    if (v == 15) S("intern. GNSS"); else NTH("undefiniert|GPS|GLONASS|GPS+GLONASS|Loran-C|Chayka|integr. Navigationssystem|vermessen|Galileo|reserviert", v > 9 ? 9 : v);
}

static void put_dims(const uint8_t *d, uint16_t n, uint16_t p)
{
    S("Abmessungen Bug "); put_u(G(p, 9));
    S(" Heck "); put_u(G(p + 9, 9));
    S(" Backbord "); put_u(G(p + 18, 6));
    S(" Steuerbord "); put_u(G(p + 24, 6));
    S(" m");
}

static void put_shiptype(uint8_t t)
{
    uint8_t c = t / 10, s = t % 10;
    S("Typ "); put_u(t); S(" ");
    if (t == 0) { S("(n.v.)"); return; }
    if (c == 3 && s < 10) {
        NTH("Fischerei|Schlepper|Schlepper gross|Baggerei|Taucher|Militaer|Segelschiff|Sportboot|reserviert|reserviert", s);
        return;
    }
    if (c == 5) {
        NTH("Lotsenfahrzeug|SAR|Schlepper|Hafenfahrzeug|Umweltschutz|Spezialfahrzeug 1|Strafverfolgung|Spezialfahrzeug 2|Sanitaet|Nicht-Kriegsschiff", s);
        return;
    }
    if (c == 1 && t < 20) { S("reserviert"); return; }
    if (c < 10) {
        NTH("|reserviert|WIG||Hochgeschw.fahrzeug||Passagierschiff|Frachtschiff|Tanker|Sonstiges", c);
        if (c != 3 && c != 5 && c != 1) {
            S(": ");
            NTH("alle|Gefahrgut A|Gefahrgut B|Gefahrgut C|Gefahrgut D|reserviert|reserviert|reserviert|reserviert|keine Zusatzinfo", s);
        }
    }
}

static void put_navstat(uint8_t s)
{
    put_u(s); sp(1);
    NTH("unter Maschine|vor Anker|manoevrierunfaehig|manoevrierbehindert|tiefgangbehindert|festgemacht|aufgelaufen|beim Fischen|unter Segel|reserviert HSC|reserviert WIG|Schleppen (Heck)|Schleppen (schiebend)|reserviert|AIS-SART aktiv|nicht definiert", s);
}

static void put_sog(uint16_t v, uint8_t tenths)
{
    S("SOG ");
    if (v == 1023) { put_nv(); return; }
    if (tenths) put_fix(v, 1); else put_u(v);
    S(" kn");
}

static void put_cog(uint16_t v)
{
    S("COG ");
    if (v >= 3600) { put_nv(); return; }
    put_fix(v, 1); S("\xC2\xB0");
}

static void put_hdg(uint16_t v)
{
    S("HDG ");
    if (v == 511) { put_nv(); return; }
    put_u(v); S("\xC2\xB0");
}

static void put_ts(uint8_t t)
{
    S("UTC-Sekunde ");
    if (t < 60) put_u(t);
    else NTH("n.v.|manuell|geschaetzt|ausgefallen", (uint8_t)(t - 60));
}

static void put_yn(uint32_t v) { SA(v, "ja", "nein"); }

/* SOTDMA/ITDMA-Kommunikationsstatus (19 Bit ab pos) */
static void put_comm(const uint8_t *d, uint16_t n, uint16_t pos, uint8_t itdma)
{
    S("Funk: ");
    SA(itdma, "ITDMA", "SOTDMA");
    S(" Sync "); put_u(G(pos, 2));
    if (itdma) {
        S(" Slot-Inkrement "); put_u(G(pos + 2, 13));
        S(" Slots "); put_u(G(pos + 15, 3));
        S(" Keep "); put_u(G(pos + 18, 1));
    } else {
        uint8_t to = (uint8_t)G(pos + 2, 3);
        S(" Timeout "); put_u(to);
        S(" Sub "); put_u(G(pos + 5, 14));
    }
}

/* Zeitstempel Basisstation */
static void put_utc(const uint8_t *d, uint16_t n, uint16_t p)
{
    uint16_t y = (uint16_t)G(p, 14);
    S("UTC ");
    if (y) put_pad(y, 4); else S("----");
    ais_putc('-'); put_pad(G(p + 14, 4), 2); ais_putc('-'); put_pad(G(p + 18, 5), 2);
    sp(1);
    put_pad(G(p + 23, 5), 2); ais_putc(':'); put_pad(G(p + 28, 6), 2); ais_putc(':'); put_pad(G(p + 34, 6), 2);
}

/* gemeinsame Koepfe */
static void head(uint8_t type, const char *name_rom, const uint8_t *d, uint16_t n)
{
    S("Typ "); put_u(type); sp(2); pstr(name_rom);
    nl(); ind();
    S("MMSI "); put_mmsi(G(8, 30)); S("  Repeat "); put_u(G(6, 2));
    S("  ("); put_u(n); S(" Bit)");
}

/* ---- Binaerdaten (Typ 6, 8, 25, 26): DAC/FI + Hexdump ---- */
static void put_binary(const uint8_t *d, uint16_t n, uint16_t dacpos, uint16_t end)
{
    uint16_t dac = (uint16_t)G(dacpos, 10);
    uint8_t  fi  = (uint8_t)G(dacpos + 10, 6);
    uint16_t p   = dacpos + 16;
    nl(); ind();
    S("Binaer DAC "); put_u(dac); S(" FI "); put_u(fi);
    if (dac == 1 && fi == 0) {                  /* IMO-Textmeldung: Ack-Flag 1, Laufnr. 11, Text */
        S(" (Textmeldung)"); nl(); ind();
        S("Ack "); put_yn(G(p, 1)); S("  Nr "); put_u(G(p + 1, 11)); S("  Text ");
        put_text_long(d, end, p + 12);
        return;
    }
    if (dac == 1 && fi == 2) S(" (Interrogation Funktionskennung)");
    if (dac == 1 && fi == 3) S(" (Fkt.-Kennungs-Kapazitaet)");
    if (dac == 1 && fi == 4) S(" (Ack-Meldung Anwendung)");
    if (dac == 1 && fi == 5) S(" (Quittung Anwendung)");
    nl(); ind();
    S("Daten ("); put_u(end > p ? end - p : 0); S(" Bit): ");
    put_hexbits(d, n, p, end);
}

/* ================================ Klartext aller Typen ================================ */
void ais_text_write(const uint8_t *d, uint16_t n)
{
    uint8_t t = (uint8_t)G(0, 6);
    uint8_t i;

    switch (t) {
    /* ---- Positionsmeldung Klasse A ---- */
    case 1: case 2: case 3: {
        int32_t rot = GS(42, 8);
        head(t, t == 1 ? PSTR("Positionsmeldung Klasse A (planmaessig)")
              : t == 2 ? PSTR("Positionsmeldung Klasse A (zugewiesen)")
                       : PSTR("Positionsmeldung Klasse A (auf Abfrage)"), d, n);
        nl(); ind();
        S("Status "); put_navstat((uint8_t)G(38, 4));
        S("  ROT ");
        if (rot == -128) put_nv();
        else {
            int32_t r = (rot * rot * 10 + 112) / 224;           /* (v/4,733)^2 Grad/min */
            if (rot == 127 || rot == -127) { SA(rot < 0, "<-", ">"); S("720"); }
            else put_fix(rot < 0 ? -r : r, 1);
            S("\xC2\xB0/min");
        }
        nl(); ind();
        put_sog((uint16_t)G(50, 10), 1); sp(2); put_cog((uint16_t)G(116, 12)); sp(2); put_hdg((uint16_t)G(128, 9));
        nl(); ind();
        put_pos(GS(61, 28), GS(89, 27));
        SA(G(60, 1), " (genau, DGNSS)", " (grob)");
        nl(); ind();
        put_ts((uint8_t)G(137, 6)); S("  Manoever ");
        NTH("n.v.|keine Besonderheit|besonderes Manoever", (uint8_t)(G(143, 2) > 2 ? 0 : G(143, 2)));
        S("  RAIM "); put_yn(G(148, 1));
        nl(); ind();
        put_comm(d, n, 149, t == 3);
        break;
    }
    /* ---- Basisstation / UTC-Antwort ---- */
    case 4: case 11:
        head(t, t == 4 ? PSTR("Basisstation-Meldung") : PSTR("UTC-/Datumsantwort"), d, n);
        nl(); ind();
        put_utc(d, n, 38);
        nl(); ind();
        put_pos(GS(79, 28), GS(107, 27));
        SA(G(78, 1), " (genau)", " (grob)");
        nl(); ind();
        S("EPFD "); put_epfd((uint8_t)G(134, 4)); S("  RAIM "); put_yn(G(148, 1));
        nl(); ind();
        put_comm(d, n, 149, 0);
        break;
    /* ---- Statische und Reisedaten Klasse A ---- */
    case 5:
        head(5, PSTR("Statische und Reisedaten Klasse A"), d, n);
        nl(); ind();
        S("AIS-Version "); put_u(G(38, 2)); S("  IMO "); put_u(G(40, 30));
        S("  Rufzeichen "); put_text(d, n, 70, 7);
        nl(); ind();
        S("Name "); put_text(d, n, 112, 20); sp(2); put_shiptype((uint8_t)G(232, 8));
        nl(); ind();
        put_dims(d, n, 240); S("  EPFD "); put_epfd((uint8_t)G(270, 4));
        nl(); ind();
        S("ETA ");
        if (G(274, 4)) put_pad(G(274, 4), 2); else S("--");
        ais_putc('-');
        if (G(278, 5)) put_pad(G(278, 5), 2); else S("--");
        sp(1);
        if (G(283, 5) < 24) put_pad(G(283, 5), 2); else S("--");
        ais_putc(':');
        if (G(288, 6) < 60) put_pad(G(288, 6), 2); else S("--");
        S("  Tiefgang "); put_fix(G(294, 8), 1); S(" m");
        nl(); ind();
        S("Ziel "); put_text(d, n, 302, 20); S("  DTE "); SA(G(422, 1), "nicht bereit", "bereit");
        break;
    /* ---- Adressierte Binaermeldung ---- */
    case 6:
        head(6, PSTR("Adressierte Binaermeldung"), d, n);
        nl(); ind();
        S("an MMSI "); put_mmsi(G(40, 30)); S("  Laufnr. "); put_u(G(38, 2));
        S("  Wiederholung "); put_yn(G(70, 1));
        put_binary(d, n, 72, n);
        break;
    /* ---- Binaerquittung / Sicherheitsquittung ---- */
    case 7: case 13:
        head(t, t == 7 ? PSTR("Binaerquittung") : PSTR("Sicherheitsquittung"), d, n);
        for (i = 0; i < 4; i++) {
            uint16_t p = (uint16_t)(40 + 32 * i);
            if (p + 32 > n) break;
            nl(); ind();
            S("quittiert MMSI "); put_mmsi(G(p, 30)); S("  Laufnr. "); put_u(G(p + 30, 2));
        }
        break;
    /* ---- Binaere Rundsendung ---- */
    case 8:
        head(8, PSTR("Binaere Rundsendung"), d, n);
        put_binary(d, n, 40, n);
        break;
    /* ---- SAR-Luftfahrzeug ---- */
    case 9:
        head(9, PSTR("Position SAR-Luftfahrzeug"), d, n);
        nl(); ind();
        S("Hoehe ");
        if (G(38, 12) == 4095) put_nv(); else { put_u(G(38, 12)); SA(G(38, 12) == 4094, " m (oder mehr)", " m"); }
        sp(2); put_sog((uint16_t)G(50, 10), 0); sp(2); put_cog((uint16_t)G(116, 12));
        nl(); ind();
        put_pos(GS(61, 28), GS(89, 27)); SA(G(60, 1), " (genau)", " (grob)");
        nl(); ind();
        put_ts((uint8_t)G(128, 6)); S("  Hoehensensor "); SA(G(134, 1), "barometrisch", "GNSS");
        S("  RAIM "); put_yn(G(147, 1));
        nl(); ind();
        S("Funk-Rohwert 0x"); { uint32_t r = G(148, 20); for (i = 20; i; i -= 4) ais_putc("0123456789ABCDEF"[(r >> (i - 4)) & 15]); }
        break;
    /* ---- UTC-Anfrage ---- */
    case 10:
        head(10, PSTR("UTC-/Datumsanfrage"), d, n);
        nl(); ind();
        S("an MMSI "); put_mmsi(G(40, 30));
        break;
    /* ---- Sicherheitstexte ---- */
    case 12:
        head(12, PSTR("Adressierte Sicherheitsmeldung"), d, n);
        nl(); ind();
        S("an MMSI "); put_mmsi(G(40, 30)); S("  Laufnr. "); put_u(G(38, 2));
        S("  Wiederholung "); put_yn(G(70, 1));
        nl(); ind(); S("Text "); put_text_long(d, n, 72);
        break;
    case 14:
        head(14, PSTR("Sicherheits-Rundsendung"), d, n);
        nl(); ind(); S("Text "); put_text_long(d, n, 40);
        break;
    /* ---- Abfrage ---- */
    case 15:
        head(15, PSTR("Abfrage"), d, n);
        nl(); ind();
        S("an MMSI "); put_mmsi(G(40, 30)); S(": Typ "); put_u(G(70, 6)); S(" Offset "); put_u(G(76, 12));
        if (n >= 110) { S(", Typ "); put_u(G(90, 6)); S(" Offset "); put_u(G(96, 12)); }
        if (n >= 160) {
            nl(); ind();
            S("an MMSI "); put_mmsi(G(110, 30)); S(": Typ "); put_u(G(140, 6)); S(" Offset "); put_u(G(146, 12));
        }
        break;
    /* ---- Zuweisungsbefehl ---- */
    case 16:
        head(16, PSTR("Zuweisungsbefehl"), d, n);
        nl(); ind();
        S("an MMSI "); put_mmsi(G(40, 30)); S("  Offset "); put_u(G(70, 12)); S("  Inkrement "); put_u(G(82, 10));
        if (n >= 144) {
            nl(); ind();
            S("an MMSI "); put_mmsi(G(92, 30)); S("  Offset "); put_u(G(122, 12)); S("  Inkrement "); put_u(G(134, 10));
        }
        break;
    /* ---- DGNSS ---- */
    case 17:
        head(17, PSTR("DGNSS-Korrekturen (RTCM 2)"), d, n);
        nl(); ind();
        S("Referenzposition ");
        { int32_t lon = GS(40, 18) * 1000L, lat = GS(58, 17) * 1000L;   /* 1/10 min -> 1/10000 min */
          put_coord(lat, 0); sp(2); put_coord(lon, 1); }
        nl(); ind();
        S("Daten ("); put_u(n > 80 ? n - 80 : 0); S(" Bit): ");
        put_hexbits(d, n, 80, n);
        break;
    /* ---- Klasse B ---- */
    case 18:
        head(18, PSTR("Positionsmeldung Klasse B"), d, n);
        nl(); ind();
        put_sog((uint16_t)G(46, 10), 1); sp(2); put_cog((uint16_t)G(112, 12)); sp(2); put_hdg((uint16_t)G(124, 9));
        nl(); ind();
        put_pos(GS(57, 28), GS(85, 27)); SA(G(56, 1), " (genau)", " (grob)");
        nl(); ind();
        put_ts((uint8_t)G(133, 6));
        S("  CS-Geraet "); SA(G(141, 1), "Carrier-Sense", "SOTDMA");
        S("  Display "); put_yn(G(142, 1)); S("  DSC "); put_yn(G(143, 1));
        nl(); ind();
        S("Band "); SA(G(144, 1), "ganzes Band", "Teilband"); S("  Typ-22 "); put_yn(G(145, 1));
        S("  zugewiesen "); put_yn(G(146, 1)); S("  RAIM "); put_yn(G(147, 1));
        break;
    case 19:
        head(19, PSTR("Erweiterte Positionsmeldung Klasse B"), d, n);
        nl(); ind();
        put_sog((uint16_t)G(46, 10), 1); sp(2); put_cog((uint16_t)G(112, 12)); sp(2); put_hdg((uint16_t)G(124, 9));
        nl(); ind();
        put_pos(GS(57, 28), GS(85, 27)); SA(G(56, 1), " (genau)", " (grob)");
        nl(); ind();
        S("Name "); put_text(d, n, 143, 20); sp(2); put_shiptype((uint8_t)G(263, 8));
        nl(); ind();
        put_dims(d, n, 271); S("  EPFD "); put_epfd((uint8_t)G(301, 4));
        nl(); ind();
        put_ts((uint8_t)G(133, 6)); S("  RAIM "); put_yn(G(305, 1)); S("  DTE "); SA(G(306, 1), "nicht bereit", "bereit");
        S("  zugewiesen "); put_yn(G(307, 1));
        break;
    /* ---- Datenlink-Verwaltung ---- */
    case 20:
        head(20, PSTR("Datenlink-Verwaltung"), d, n);
        for (i = 0; i < 4; i++) {
            uint16_t p = (uint16_t)(40 + 30 * i);
            if (p + 30 > n) break;
            if (i && G(p, 12) == 0) continue;
            nl(); ind();
            S("Reservierung "); put_u(i + 1);
            S(": Offset "); put_u(G(p, 12)); S("  Slots "); put_u(G(p + 12, 4));
            S("  Timeout "); put_u(G(p + 16, 3)); S(" min  Inkrement "); put_u(G(p + 19, 11));
        }
        break;
    /* ---- Hilfsmittel der Navigation ---- */
    case 21: {
        head(21, PSTR("Hilfsmittel der Navigation (AtoN)"), d, n);
        nl(); ind();
        S("Typ "); put_u(G(38, 5)); sp(1);
        NTH("n.v.|Bezugspunkt|RACON|Festbauwerk|Notfall-Wrack-Markierung|Leuchtfeuer ohne Sektoren|Leuchtfeuer mit Sektoren|Leitfeuer|Leitfeuer (Fluchtlinie)|Kardinaltonne N|Kardinaltonne O|Kardinaltonne S|Kardinaltonne W|Fahrwasser-Backbord|Fahrwasser-Steuerbord|Fahrwasser-Backbord bevorzugt|Fahrwasser-Steuerbord bevorzugt|Einzelgefahr|Sicheres Wasser|Sonderzeichen|Leuchtschiff/LANBY|reserviert", (uint8_t)G(38, 5));
        nl(); ind();
        S("Name "); put_text(d, n, 43, 20);
        if (n > 272) { uint8_t k = (uint8_t)((n - 272) / 6); if (k > 14) k = 14; S(" + "); put_text(d, n, 272, k); }
        nl(); ind();
        put_pos(GS(164, 28), GS(192, 27)); SA(G(163, 1), " (genau)", " (grob)");
        nl(); ind();
        put_dims(d, n, 219); S("  EPFD "); put_epfd((uint8_t)G(249, 4));
        nl(); ind();
        put_ts((uint8_t)G(253, 6)); S("  ausser Position "); put_yn(G(259, 1));
        S("  virtuell "); put_yn(G(269, 1)); S("  RAIM "); put_yn(G(268, 1)); S("  zugewiesen "); put_yn(G(270, 1));
        break;
    }
    /* ---- Kanalverwaltung ---- */
    case 22:
        head(22, PSTR("Kanalverwaltung"), d, n);
        nl(); ind();
        S("Kanal A "); put_u(G(40, 12)); S("  Kanal B "); put_u(G(52, 12));
        S("  Modus "); NTH("TX A+B/RX A+B|TX A/RX A+B|TX B/RX A+B|reserviert|nur RX A+B|reserviert", (uint8_t)(G(64, 4) > 5 ? 5 : G(64, 4)));
        S("  Leistung "); SA(G(68, 1), "niedrig", "hoch");
        nl(); ind();
        if (G(139, 1)) {
            S("adressiert an MMSI "); put_mmsi(G(69, 30)); S(" und "); put_mmsi(G(104, 30));
        } else {
            S("Gebiet NO "); put_coord(GS(87, 17) * 1000L, 0); sp(1); put_coord(GS(69, 18) * 1000L, 1);
            S("  SW "); put_coord(GS(122, 17) * 1000L, 0); sp(1); put_coord(GS(104, 18) * 1000L, 1);
        }
        nl(); ind();
        S("Band A "); SA(G(140, 1), "schmal", "Standard"); S("  Band B "); SA(G(141, 1), "schmal", "Standard");
        S("  Zonengroesse "); put_u(G(142, 3) + 1); S(" sm");
        break;
    /* ---- Gruppenzuweisung ---- */
    case 23:
        head(23, PSTR("Gruppenzuweisung"), d, n);
        nl(); ind();
        S("Gebiet NO "); put_coord(GS(58, 17) * 1000L, 0); sp(1); put_coord(GS(40, 18) * 1000L, 1);
        S("  SW "); put_coord(GS(93, 17) * 1000L, 0); sp(1); put_coord(GS(75, 18) * 1000L, 1);
        nl(); ind();
        S("Stationstyp "); put_u(G(110, 4)); S("  Schiffstyp "); put_u(G(114, 8));
        S("  TX/RX "); put_u(G(144, 2)); S("  Meldeintervall "); put_u(G(146, 4));
        S("  Ruhezeit "); put_u(G(150, 4)); S(" min");
        break;
    /* ---- Statische Daten Klasse B ---- */
    case 24:
        head(24, G(38, 2) ? PSTR("Statische Daten Klasse B, Teil B") : PSTR("Statische Daten Klasse B, Teil A"), d, n);
        nl(); ind();
        if (G(38, 2) == 0) {
            S("Name "); put_text(d, n, 40, 20);
        } else {
            S("Typ "); put_shiptype((uint8_t)G(40, 8)); S("  Hersteller "); put_text(d, n, 48, 3);
            S("  Modell "); put_u(G(66, 4)); S("  Serie "); put_u(G(70, 20));
            nl(); ind();
            S("Rufzeichen "); put_text(d, n, 90, 7); sp(2);
            if (G(8, 30) / 10000000UL == 98) { S("Mutterschiff MMSI "); put_mmsi(G(132, 30)); }
            else put_dims(d, n, 132);
        }
        break;
    /* ---- Einzel-/Mehrslot-Binaer ---- */
    case 25: case 26: {
        uint16_t p = 40;
        uint16_t end = n;
        head(t, t == 25 ? PSTR("Einzelslot-Binaermeldung") : PSTR("Mehrslot-Binaermeldung"), d, n);
        nl(); ind();
        if (G(38, 1)) { S("adressiert an MMSI "); put_mmsi(G(40, 30)); p = 70; } else S("Rundsendung");
        SA(G(39, 1), "  strukturiert", "  unstrukturiert");
        if (t == 26 && end > p + 20) end -= 20;                 /* Funkstatus am Ende */
        if (G(39, 1)) put_binary(d, n, p, end);
        else { nl(); ind(); S("Daten ("); put_u(end > p ? end - p : 0); S(" Bit): "); put_hexbits(d, n, p, end); }
        if (t == 26) { nl(); ind(); S("Funkstatus 0x"); { uint32_t r = G(end, 20); for (i = 20; i; i -= 4) ais_putc("0123456789ABCDEF"[(r >> (i - 4)) & 15]); } }
        break;
    }
    /* ---- Langstrecken ---- */
    case 27:
        head(27, PSTR("Langstrecken-Positionsmeldung"), d, n);
        nl(); ind();
        S("Status "); put_navstat((uint8_t)G(40, 4));
        S("  "); put_sog((uint16_t)G(79, 6), 0); sp(2);
        S("COG "); if (G(85, 9) >= 360) put_nv(); else { put_u(G(85, 9)); S("\xC2\xB0"); }
        nl(); ind();
        put_pos(GS(44, 18) * 1000L, GS(62, 17) * 1000L);
        SA(G(38, 1), " (genau)", " (grob)");
        S("  RAIM "); put_yn(G(39, 1)); S("  GNSS "); SA(G(94, 1), "nicht aktuell", "aktuell");
        break;
    default:
        S("Typ "); put_u(t); S(" (unbekannt)");
        break;
    }
    nl();
}
