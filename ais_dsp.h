/*
 * ais_dsp.h - AIS-Empfangskern: Abtastwerte rein, rohe HDLC-Rahmen raus
 */
#ifndef AIS_DSP_H
#define AIS_DSP_H

#include "ais_port.h"

/* Abtastwerte je Bit.  Die Abtastrate ist 9600 * AIS_OSR:  4 -> 38,4 kHz (ATmega328P),
 * 5 -> 48 kHz (PC/ESP32/Soundkarte).  Ganzzahlig, damit die Bitphasen exakt auf dem Raster liegen. */
#ifndef AIS_OSR
#define AIS_OSR 4
#endif
#define AIS_FS (9600UL * AIS_OSR)

/* groesster Rahmen: 5 Slots = 1008 Nutzbits + 16 Bit FCS (+ Reserve fuer Stopfbits-Schlupf).
 * Der Puffer enthaelt zusaetzlich die 7 Bit des Endflags (0 111111), die erst nachtraeglich
 * als Flag erkannt werden. */
#define AIS_FRAME_MAXBITS 1040
#define AIS_FRAME_BYTES   ((AIS_FRAME_MAXBITS + 7 + 7) / 8 + 1)
/* Pufferplaetze: je Bitphase ein Sammler + Reserve fuer fertige, noch nicht geprueft/ausgegebene Rahmen */
#define AIS_NSLOTS        (AIS_OSR + 2)

typedef struct {
    uint16_t nbits;                  /* Bits OHNE Flags, MIT FCS (noch nicht geprueft)       */
    uint16_t level;                  /* mittlere Augenoeffnung der Bitphase (relativ, Debug)  */
    uint16_t tick;                   /* Abtastzaehler (niederwertige 16 Bit) beim Rahmenende  */
    uint8_t  d[AIS_FRAME_BYTES];     /* Luftreihenfolge: je Byte LSB zuerst gefuellt          */
} ais_frame_t;

typedef struct {
    uint16_t frames;                 /* Rahmen komplett empfangen (Flag bis Flag)             */
    uint16_t overflow;               /* Rahmen verworfen: kein freier Pufferplatz            */
    uint16_t aborts;                 /* Rahmen abgebrochen (>= 7 Einsen oder zu lang)         */
} ais_dsp_stat_t;

extern ais_dsp_stat_t ais_dsp_stat;

void ais_dsp_init(void);

/* Ein Abtastwert, 10 Bit (0..1023), in gleichmaessigem Abstand 1/AIS_FS.
 * Die Absolutlage ist egal (Gleichanteil wird nachgefuehrt), nur Vorzeichen des Tonhubs zaehlt:
 * ob "Mark hoch" oder "tief" ist, ist wegen der NRZI-Codierung ohne Bedeutung. */
void ais_dsp_sample(uint16_t x);

/* Warteschlange fertiger Rahmen (Erzeuger: ais_dsp_sample, Verbraucher: Hauptprogramm) */
const ais_frame_t *ais_frame_peek(void);   /* aeltester Rahmen oder 0 */
void ais_frame_release(void);

#endif
