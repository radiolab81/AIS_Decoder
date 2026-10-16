/*
 * main.c - AIS-Empfaenger fuer ATmega328P, bare metal (avr-gcc / avr-libc, kein Arduino-Core)
 * ========================================================================================
 *
 * Ein NFM-Scanner (oder Funkgeraet) auf 161,975 MHz (AIS 1, "A") oder 162,025 MHz (AIS 2, "B"),
 * NF-Ausgang direkt hinter dem FM-Diskriminator -> ADC0.  Ausgabe: gueltige AIS-Nachrichten
 * als NMEA-0183-Saetze (!AIVDM) auf USART0, 38400 8N1 (die uebliche AIS-NMEA-Rate).
 *
 * Hardware (Eingangsbeschaltung)
 *
 *                      Vcc
 *                       |
 *                      [10k]
 *   NF ----||----+------+------[1k]------+---- ADC0 (A0)      Eingang auf Vcc/2 vorspannen:
 *       100 nF   |                       |                    2 x 10k Teiler, NF ueber 100 nF
 *               [10k]                  [22n]                  eingekoppelt; 1k + 22 nF = Tiefpass
 *                |                       |                    ca. 7 kHz (Anti-Alias, 4 x 9600 Hz
 *               GND                     GND                   Abtastung).
 *
 *   Pegel: der AIS-Hub (+-2,4 kHz) soll am ADC mindestens ca. +-50 LSB ausmachen, ohne anzustossen.
 *   Die Abtastrate 38,4 kHz = 4 x 9600 erzeugt Timer1 exakt aus dem Quarz (siehe unten).
 *
 * Serielle Schnittstelle (USART0, 38400 8N1)
 *   TX: !AIVDM-Saetze (und bei Bedarf Klartext).   RX: Einzelzeichen-Befehle (ais_app_cmd):
 *   n NMEA ein/aus, d Klartext ein/aus, a/b Kanal, s Statistik, ? Hilfe.
 *
 * Zeitliche Aufteilung
 *   ISR(ADC_vect) laeuft 38400 mal pro Sekunde und ruft ais_dsp_sample() (Bitdemodulation,
 *   Flag, Entstopfen).  Die Hauptschleife prueft die CRC, formatiert NMEA/Klartext und
 *   sendet.  Mit -DDEBUG_LOAD liegt PB0 (Arduino Pin 8) waehrend der ISR auf high; das
 *   Tastverhaeltnis ist die Rechenlast der Interruptkette.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "ais_app.h"

#ifndef AIS_BAUD
#define AIS_BAUD 38400UL
#endif

/* ---- UART (Polling, blockierend; die ISR laeuft dabei weiter) --------------------------- */
void ais_putc(char c)
{
    while (!(UCSR0A & _BV(UDRE0))) { }
    UDR0 = (uint8_t)c;
}

static void uart_init(void)
{
    UBRR0 = (uint16_t)((F_CPU + 4UL * AIS_BAUD) / (8UL * AIS_BAUD) - 1);
    UCSR0A = _BV(U2X0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);                 /* 8N1 */
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
}

/* ---- Abtasttakt: Timer1 + ADC im Auto-Trigger-Betrieb ----------------------------------- */
/* 16 MHz / 38400 = 416,667 Takte.  Timer1 (CTC) zaehlt abwechselnd 416, 417, 417 Takte
 * (Summe 1250 = 3 * 416,667): im Mittel exakt 38,400 kHz, nur +-1 Takt (62 ns) Jitter.
 * Der Timer loest den ADC ueber "Compare Match B" selbst aus - kein ISR-Jitter im Abtastzeitpunkt. */
#define TOP_A  (F_CPU / (9600UL * AIS_OSR) - 1)          /* 415 */

static volatile uint8_t  rot;                            /* 0,1,2: welche der drei Periodenlaengen */

static void adc_timer_init(void)
{
    DIDR0 = _BV(ADC0D);                                  /* digitalen Eingang von A0 abschalten */
    ADMUX = _BV(REFS0);                                  /* Referenz AVcc, Kanal 0, rechtsbuendig */
#ifdef AIS_SIM        /* nur Simulation: simavr schliesst keine ADC-Wandlung ab; die Timer-ISR liest die vom Simulator gesetzten ADC-Register */
    TIMSK1 = _BV(OCIE1A);
#else
    ADCSRB = _BV(ADTS2) | _BV(ADTS0);                    /* Trigger: Timer1 Compare Match B */
    ADCSRA = _BV(ADEN) | _BV(ADATE) | _BV(ADIE) | _BV(ADPS2);   /* Takt 16 MHz/16 = 1 MHz: 13 us */
#endif
    OCR1A = TOP_A;
    OCR1B = 8;
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS10);                     /* CTC, TOP = OCR1A, kein Vorteiler */
}

#ifdef AIS_SIM
ISR(TIMER1_COMPA_vect)
#else
ISR(ADC_vect)
#endif
{
    uint16_t x = ADC;
#ifdef DEBUG_LOAD
    PORTB |= _BV(PB0);
#endif
    TIFR1 = _BV(OCF1B);                                  /* Trigger-Flag loeschen, damit die naechste Flanke zaehlt */
    /* Periode 416 / 417 / 417: OCR1A = TOP */
    {
        uint8_t r = rot;
        OCR1A = (uint16_t)(r == 0 ? TOP_A : TOP_A + 1);
        rot = (uint8_t)(r >= 2 ? 0 : r + 1);
    }
    ais_dsp_sample(x);
#ifdef DEBUG_LOAD
    PORTB &= (uint8_t)~_BV(PB0);
#endif
}

int main(void)
{
#ifdef DEBUG_LOAD
    DDRB |= _BV(PB0);
#endif
    DDRB |= _BV(PB5);                                    /* LED (Nano: D13) blitzt bei gueltiger Nachricht */
    uart_init();
    ais_app_init();
    adc_timer_init();
    sei();

    if (AIS_DEFAULT_TEXT) ais_app_cmd('?');
    for (;;) {
        if (ais_app_poll()) PINB = _BV(PB5);             /* Pin toggeln */
#ifndef AIS_NO_RX                                        /* (simavr bremst jedes UCSR0A-Lesen per usleep) */
        if (UCSR0A & _BV(RXC0)) ais_app_cmd((char)UDR0);
#endif
    }
}
