/*
 * ais_app.h - Hauptschleife des Decoders (gemeinsam fuer AVR, PC-Simulation, ESP32)
 */
#ifndef AIS_APP_H
#define AIS_APP_H

#include "ais_msg.h"

/* Voreinstellung der Ausgabe (zur Laufzeit per Tastenkommando aenderbar, siehe ais_app_cmd) */
#ifndef AIS_DEFAULT_NMEA
#define AIS_DEFAULT_NMEA 1               /* 1 = !AIVDM-Saetze ausgeben                   */
#endif
#ifndef AIS_DEFAULT_TEXT
#define AIS_DEFAULT_TEXT 0               /* 1 = zusaetzlich Klartext (Debug)             */
#endif
#ifndef AIS_DEFAULT_CHAN
#define AIS_DEFAULT_CHAN 'A'             /* 'A' = 161,975 MHz, 'B' = 162,025 MHz         */
#endif

typedef struct {
    uint16_t ok;                         /* gueltige Nachrichten ausgegeben                */
    uint16_t bad_crc;                    /* Rahmen mit falscher Pruefsumme                  */
    uint16_t bad_type;                   /* CRC ok, aber Typ/Laenge unzulaessig             */
    uint16_t dup;                        /* Doppeltreffer (andere Bitphase, gleicher Burst)   */
} ais_app_stat_t;

extern ais_app_stat_t ais_app_stat;

void ais_app_init(void);

/* Aus der Hauptschleife aufrufen: verarbeitet alle fertigen Rahmen.  Liefert die Zahl der
 * ausgegebenen Nachrichten. */
uint8_t ais_app_poll(void);

/* Tastenkommando (ein Zeichen vom UART):
 *   n  NMEA ein/aus      d  Klartext (Debug) ein/aus      a / b  Kanal A / B
 *   s  Statistik         ?  Hilfe                                                      */
void ais_app_cmd(char c);

#endif
