/*
 * ais_port.h - kleine Portabilitaetsschicht (AVR <-> PC <-> ESP32)
 * ================================================================
 * Der Decoderkern (ais_dsp.c, ais_msg.c) kennt weder ADC noch UART.  Er braucht nur
 *   - ein paar Typen,
 *   - Zugriff auf Konstanten im Flash (AVR: PROGMEM, sonst normaler Speicher),
 *   - eine Ausgabefunktion  void ais_putc(char c)  die der Anwender bereitstellt.
 */
#ifndef AIS_PORT_H
#define AIS_PORT_H

#include <stdint.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#define AIS_ROM            PROGMEM
#define ais_rom_u8(p)      pgm_read_byte(p)
#define ais_rom_u16(p)     pgm_read_word(p)
#else
#define AIS_ROM
#define ais_rom_u8(p)      (*(const uint8_t *)(p))
#define ais_rom_u16(p)     (*(const uint16_t *)(p))
#endif

/* vom Anwender bereitzustellen: ein Zeichen auf die serielle Schnittstelle (darf blockieren) */
extern void ais_putc(char c);

#endif
