/*
 * ais_msg.h - Rahmenpruefung, NMEA-0183-Ausgabe (!AIVDM) und Klartext aller AIS-Nachrichten
 */
#ifndef AIS_MSG_H
#define AIS_MSG_H

#include "ais_dsp.h"

/* 1 = auch Rahmen mit gueltiger CRC verwerfen, deren Nachrichtentyp unbekannt ist (0, 28..63)
 *     oder deren Laenge fuer den Typ nicht zulaessig ist (ITU-R M.1371-5) */
#ifndef AIS_STRICT
#define AIS_STRICT 1
#endif

enum { AIS_OK = 0, AIS_ERR_SHORT, AIS_ERR_CRC, AIS_ERR_TYPE, AIS_ERR_LEN };

/* Prueft CRC-16/X.25 und Typ/Laenge.  Ergebnis: Anzahl Nutzbits (ohne FCS) oder 0;
 * *err bekommt dann den Grund (AIS_ERR_...) */
uint16_t ais_frame_check(const ais_frame_t *f, uint8_t *err);

/* Bits der Nachricht in NMEA-Reihenfolge (MSB zuerst), Feld pos .. pos+len-1, len <= 32 */
uint32_t ais_get(const uint8_t *d, uint16_t nbits, uint16_t pos, uint8_t len);

/* !AIVDM-Satz(e) ausgeben.  chan = 'A' oder 'B', seq = laufende Nummer 0..9 fuer Mehrteiler.
 * Gibt die Anzahl der Saetze zurueck (1..3).  Ausgabe ueber ais_putc(). */
uint8_t ais_nmea_write(const uint8_t *d, uint16_t nbits, char chan, uint8_t seq);

/* Nachricht als formatierten Klartext ausgeben (alle Typen 1..27) */
void ais_text_write(const uint8_t *d, uint16_t nbits);

#endif
