/*
 * ais_dsp.c - AIS-Empfangskern (ITU-R M.1371-5): Abtastwerte rein, rohe HDLC-Rahmen raus
 * =====================================================================================
 * Festkomma, ohne Gleitkomma, ohne Division, ohne Heap.  Laeuft unveraendert auf AVR, PC, ESP32.
 * Diese Datei ist als Lehrbeispiel kommentiert: Sie zeigt in ~100 Zeilen, wie ein
 * Paketfunk-Empfaenger vom Spannungsverlauf bis zum fertigen Rahmen kommt.
 *
 * 1. WAS DER SENDER MACHT
 * -----------------------
 * Ein AIS-Sender packt eine Nachricht (meist 168 Bit) in einen HDLC-Rahmen:
 *
 *   Training   Startflag   Nutzdaten (Typ, MMSI, ...)  FCS     Endflag   Puffer
 *   24 Bit     01111110    168 .. 1008 Bit, gestopft  16 Bit   01111110
 *   0101...    (0x7E)      nach 5 Einsen eine 0       CRC-16   (0x7E)
 *
 *  - Bitstopfen: In den Daten kommen nie sechs Einsen hintereinander vor, denn "01111110"
 *    ist das Flag.  Der Sender fuegt deshalb nach jeder 5. Eins eine 0 ein, der Empfaenger
 *    entfernt sie wieder.  Sieben Einsen in Folge sind verboten (= Abbruch).
 *  - NRZI: Eine 0 wird als Pegelwechsel gesendet, eine 1 als "Pegel bleibt".  Dadurch ist
 *    nur der WECHSEL wichtig, nicht ob der Pegel "hoch" oder "tief" ist, und lange 1-Folgen
 *    sind gerade Linien.  (Folge: Polaritaet und Gleichanteil des Signals sind egal.)
 *  - GMSK (BT = 0,4): Die Bits schalten die Frequenz um +-2,4 kHz um, 9600 mal pro Sekunde;
 *    ein Gauss-Filter rundet die Flanken ab (Bandbreite!).  Das Bit n ist deshalb auch ein
 *    wenig von den Nachbarbits beeinflusst (Intersymbol-Interferenz).
 *
 * 2. WAS DER EMPFAENGER LIEFERT (und warum hier kein I/Q gebraucht wird)
 * ----------------------------------------------------------------------
 * Ein Schmalband-FM-Empfaenger (NFM-Scanner) demoduliert genau das: am NF-Ausgang liegt die
 * Datenfolge als verrundete Rechteckspannung (+-2,4 kHz Hub = hoch / tief).  Ein I/Q-Mischer
 * mit Traegernachfuehrung ist deshalb NICHT noetig.  Die Traegerablage (Quarzfehler von Sender
 * und Empfaenger, bis ca. +-1 kHz) erscheint als Gleichspannungsanteil und wird von der
 * Schwellennachfuehrung (Abschnitt 4) aufgefangen.  (Bei I/Q-Aufnahmen rechnet man vorher einen
 * Diskriminator Im{z[n] * conj(z[n-1])} und macht dann genauso weiter, siehe tools/wav2raw.py --iq.)
 *
 * 3. BITTAKT OHNE PLL: PHASENDIVERSITAET
 * --------------------------------------
 * Der Empfaenger tastet mit AIS_OSR Werten pro Bit ab (4 = 38,4 kHz).  Die Lage dieses Rasters zum
 * Bittakt des Senders ist beim Burstbeginn voellig unbekannt, und eine PLL braeuchte zum Einrasten
 * Zeit, die bei nur 24 Trainingsbits knapp ist.  Darum:
 *
 *   * Jeder der AIS_OSR Abtastzeitpunkte pro Bit ist eine eigene "Bitphase" mit einem
 *     kompletten eigenen Empfaenger (NRZI-Entscheidung, Flagsuche, Entstopfen, Rahmenpuffer).
 *   * Pro Abtastwert arbeitet genau EIN Empfaenger (der seiner Phase) - die Rechenlast ist
 *     dieselbe wie bei einem einzigen.
 *   * Alle Phasen dekodieren denselben Burst.  Die Phasen nahe am idealen Zeitpunkt liefern
 *     einen fehlerfreien Rahmen (CRC ok); die ungeeigneten Muell (CRC falsch, wird verworfen).
 *     Doppelte Treffer entfernt ais_app.c.  Es muss nichts "gewaehlt" oder eingerastet werden.
 *
 * 4. SIGNALKETTE IN ais_dsp_sample()
 * ----------------------------------
 *   ADC --38,4 kHz--> [Integrate&Dump: y = Summe der letzten AIS_OSR Werte = Mittel ueber 1 Bit]
 *                       |   (Mitteln = Tiefpass, unterdrueckt Rauschen, matched filter fuer Rechteck)
 *                       v   (jeweils in der Bitphase p = n mod AIS_OSR)
 *                [Schwelle thr = Mitte aus Hoch-/Tiefpegel-Tracker]
 *                       |
 *                [Hartentscheidung y > thr] -> [NRZI: gleich = 1, Wechsel = 0]
 *                       |
 *                [8-Bit-Schieberegister == 0x7E ?] -> Rahmenanfang/-ende (on_flag)
 *                       |
 *                [Entstopfen, in Rahmenpuffer schreiben] (collect)
 *
 * Die Pruefsumme (CRC-16/X.25) rechnet das Hauptprogramm (ais_msg.c); die Interruptroutine
 * bleibt kurz.
 */
#include "ais_dsp.h"

/* Entscheidungsschwelle = Mitte zwischen dem mittleren oberen und unteren Pegel (je Phase).
 * Jeder Pegel wird nur von "seiner" Seite mit 2^-AIS_LVL_SHIFT je Entscheidung (Zeitkonstante
 * 16 Bit) nachgefuehrt; der jeweils andere zieht nur sehr langsam (2^-AIS_LEAK_SHIFT) mit.
 * Folgen gleicher Bits (Flag: 6 gleiche) verschieben die Schwelle dadurch kaum, die
 * Frequenzablage (Gleichanteil) wird aber schon innerhalb der 24-Bit-Trainingsfolge erfasst.
 * Es gibt keinen festen ADC-Nullpunkt: nur der Startwert liegt in der ADC-Mitte.
 * Grenze: Ein Hochpass im Signalweg (z. B. 300 Hz bei Sprachkanaelen) laesst lange Gleichpegel-
 * Folgen "durchhaengen"; das fuehrt diese Schwelle nicht nach (siehe README, bekannte Grenzen). */
#ifndef AIS_LVL_SHIFT
#define AIS_LVL_SHIFT 4
#endif
#ifndef AIS_LEAK_SHIFT
#define AIS_LEAK_SHIFT 8                 /* 8: nur Byte-Zugriff, auf dem AVR fast gratis */
#endif
/* kuerzeste plausible Rahmenlaenge in Bit (Typ 14 mit 0 Zeichen: 40 + 16 FCS) */
#define AIS_MINBITS 56
#define NOSLOT 0xFF

ais_dsp_stat_t ais_dsp_stat;

/* ---- Abtastwerte / Filter ------------------------------------------------------------ */
static uint16_t ring[AIS_OSR];       /* die letzten AIS_OSR Abtastwerte                       */
static uint8_t  ri;                  /* Schreibindex im Ring                                  */
static uint16_t ysum;                /* Summe des Rings = Integrate&Dump ueber 1 Bit (10 Bit * OSR * 4
                                        passt bis OSR 8 in 16 Bit) */
static uint16_t tick;                /* Abtastzaehler                                         */

/* ---- Empfaenger je Bitphase ------------------------------------------------------------
 * Alles, was eine Phase braucht, in einer Struktur: der Compiler adressiert dann ueber einen
 * Zeiger (billig), statt bei jedem Zugriff Feldindex * Elementgroesse zu rechnen. */
typedef struct {
    int16_t  hi, lo;                 /* mittlerer Pegel der oberen / unteren Tonlage (y*4)     */
    uint16_t cnt;                    /* gesammelte Bits (inkl. der 7 Flag-Bits am Ende)        */
    uint8_t *wp;                     /* Schreibzeiger im Rahmenpuffer (aktuelles Byte)         */
    uint8_t  mask;                   /* Bitmaske im aktuellen Byte (LSB zuerst)                */
    uint8_t  sh;                     /* die letzten 8 NRZI-decodierten Bits                    */
    uint8_t  prv;                    /* vorige Hartentscheidung (fuer NRZI)                    */
    uint8_t  ones;                   /* aufeinanderfolgende Einsen im Datenstrom               */
    uint8_t  slot;                   /* Pufferplatz dieser Phase (NOSLOT = sammelt nicht)      */
} phase_t;

static phase_t  P[AIS_OSR];
static phase_t *cur;                 /* Phase des naechsten Abtastwerts                        */

/* ---- Pufferplaetze und Warteschlange ----------------------------------------------------- */
enum { S_FREE = 0, S_BUSY = 1, S_READY = 2 };
static ais_frame_t fr[AIS_NSLOTS];
static uint8_t  state[AIS_NSLOTS];
static uint8_t  queue[AIS_NSLOTS];   /* Ring fertiger Plaetze */
static volatile uint8_t qw, qr;

void ais_dsp_init(void)
{
    uint8_t i;
    for (i = 0; i < AIS_OSR; i++) {
        ring[i] = 0;
        P[i].hi = P[i].lo = (int16_t)(2048 * AIS_OSR);
        P[i].sh = 0; P[i].prv = 0; P[i].ones = 0; P[i].cnt = 0; P[i].slot = NOSLOT;
    }
    for (i = 0; i < AIS_NSLOTS; i++) state[i] = S_FREE;
    ri = 0; ysum = 0; tick = 0; qw = qr = 0; cur = &P[0];
    ais_dsp_stat.frames = ais_dsp_stat.overflow = ais_dsp_stat.aborts = 0;
}

const ais_frame_t *ais_frame_peek(void)
{
    if (qr == qw) return 0;
    return &fr[queue[qr]];
}

void ais_frame_release(void)
{
    uint8_t n = qr;
    state[queue[n]] = S_FREE;
    if (++n >= AIS_NSLOTS) n = 0;
    qr = n;
}

/* Sammeln in Platz s beginnen; ist keiner frei, sammelt diese Phase nicht */
static void start(phase_t *f, uint8_t s)
{
    f->slot = s; f->cnt = 0; f->ones = 0;
    if (s != NOSLOT) { f->wp = fr[s].d; *f->wp = 0; f->mask = 1; }
}

static uint8_t alloc(void)
{
    uint8_t i;
    for (i = 0; i < AIS_NSLOTS; i++) {
        if (state[i] == S_FREE) { state[i] = S_BUSY; return i; }
    }
    ais_dsp_stat.overflow++;
    return NOSLOT;
}

/* Flag in dieser Phase erkannt: laufenden Rahmen abschliessen und neuen beginnen */
static void on_flag(phase_t *f)
{
    uint8_t s = f->slot;
    if (s != NOSLOT && f->cnt >= AIS_MINBITS + 7) {
        fr[s].nbits = f->cnt - 7;                 /* die 7 Flag-Bits (0 111111) waren schon im Puffer */
        fr[s].level = (uint16_t)(f->hi - f->lo);
        fr[s].tick  = tick;
        state[s] = S_READY;
        queue[qw] = s;
        { uint8_t n = qw + 1; if (n >= AIS_NSLOTS) n = 0; qw = n; }
        ais_dsp_stat.frames++;
        s = NOSLOT;                               /* Platz gehoert jetzt der Warteschlange */
    }
    if (s == NOSLOT) s = alloc();                 /* sonst den bisherigen Platz weiterverwenden */
    start(f, s);
}

static void drop(phase_t *f)
{
    state[f->slot] = S_FREE; f->slot = NOSLOT;
    ais_dsp_stat.aborts++;
}

/* ein NRZI-decodiertes Bit in den Rahmenpuffer dieser Phase (Bitstopfen entfernen) */
static inline void collect(phase_t *f, uint8_t b)
{
    if (b) {
        if (++f->ones >= 7) { drop(f); return; }   /* sechs Einsen nur im Flag, sieben nie */
    } else {
        uint8_t o = f->ones;
        f->ones = 0;
        if (o == 5) return;                        /* Stopfbit: nach fuenf Einsen eine 0, nicht speichern */
    }
    if (f->cnt >= AIS_FRAME_MAXBITS + 7) { drop(f); return; }
    if (b) *f->wp |= f->mask;
    f->mask <<= 1;
    if (!f->mask) { f->mask = 1; *++f->wp = 0; }
    f->cnt++;
}

void ais_dsp_sample(uint16_t x)
{
    phase_t *f = cur;
    uint16_t y4;
    int16_t thr, y;
    uint8_t bit, b;

    /* --- Integrate & Dump: gleitende Summe ueber genau 1 Bit --- */
    ysum += x - ring[ri];
    ring[ri] = x;
    if (++ri >= AIS_OSR) ri = 0;
    y4 = (uint16_t)(ysum << 2);
    y = (int16_t)y4;
    tick++;

    /* --- Schwelle = Mitte der beiden Pegel; jeder Pegel folgt nur "seiner" Seite (siehe oben) --- */
    thr = (int16_t)((f->hi + f->lo) >> 1);
    if (y > thr) {
        b = 1;
        f->hi += (y - f->hi) >> AIS_LVL_SHIFT;
        f->lo += (y - f->lo) >> AIS_LEAK_SHIFT;
    } else {
        b = 0;
        f->lo += (y - f->lo) >> AIS_LVL_SHIFT;
        f->hi += (y - f->hi) >> AIS_LEAK_SHIFT;
    }

    /* --- NRZI (gleich = 1, Wechsel = 0), Schieberegister, Flag ---
     * Die Reihenfolge im Register ist egal: 01111110 ist spiegelsymmetrisch. */
    bit = (uint8_t)((b ^ f->prv) ^ 1);
    f->prv = b;
    f->sh = (uint8_t)((f->sh >> 1) | (bit << 7));

    if (f->sh == 0x7E) on_flag(f);
    else if (f->slot != NOSLOT) collect(f, bit);

    if (++f == &P[AIS_OSR]) f = &P[0];
    cur = f;
}
