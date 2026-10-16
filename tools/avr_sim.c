/*
 * avr_sim.c - laesst die ECHTE Firmware (main.elf) auf einem simulierten ATmega328P laufen
 *
 *   avr_sim main.elf datei.raw [max_sekunden]
 *
 * Eingabe wie bei host_sim: uint16 little endian, 10 Bit, 38,4 kHz.  Jeder Wert wird als
 * Spannung an ADC0 gelegt; Timer1, ADC, ISR, UART laufen als Maschinencode in simavr.  Alles,
 * was die Firmware auf USART0 sendet, kommt auf stdout.  Ist die Firmware mit -DDEBUG_LOAD
 * gebaut, misst die Simulation ueber PB0 die Rechenlast der ISR (Durchschnitt und Maximum).
 * Bauen:  make avr_sim   (braucht libsimavr-dev und libelf-dev)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_ioport.h>
#include <simavr/sim_irq.h>
#include <simavr/avr_ioport.h>

static avr_t *avr;
static avr_irq_t *adc0;
static FILE *fin;
static uint64_t n_fed;
static int eof;
static uint64_t t_rise, load_sum, load_max, isr_cnt;

static avr_cycle_count_t feed(avr_t *a, avr_cycle_count_t when, void *p)
{
    uint16_t x;
    (void)p; (void)a; (void)when;
    if (fread(&x, 2, 1, fin) != 1) { eof = 1; return 0; }
    avr_raise_irq(adc0, (uint32_t)(((uint32_t)x * 5000 + 512) / 1024));
    if (getenv("AVR_TRACE") && n_fed < 40) fprintf(stderr, "feed %llu cyc %llu ADCSRA=%02x ADC=%02x%02x\n", (unsigned long long)n_fed, (unsigned long long)avr->cycle, avr->data[0x7A], avr->data[0x79], avr->data[0x78]);
    n_fed++;
    /* Firmware (AIS_SIM) liest die ADC-Register direkt */
    avr->data[0x78] = (uint8_t)(x & 0xFF); avr->data[0x79] = (uint8_t)(x >> 8);
    /* naechster Abtastzeitpunkt: n * 16e6/38400 = n * 416,667 Takte (aus 416/417/417) */
    return (avr_cycle_count_t)(100 + (uint64_t)(n_fed) * 1250 / 3);
}

static void uart_out(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    putchar((int)(value & 0xFF));
}

static void pb(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (value) { t_rise = avr->cycle; isr_cnt++; }
    else if (t_rise) {
        uint64_t d = avr->cycle - t_rise;
        load_sum += d; if (d > load_max) load_max = d;
        t_rise = 0;
    }
}

int main(int argc, char **argv)
{
    elf_firmware_t f;
    double maxs = argc > 3 ? atof(argv[3]) : 1e9;
    if (argc < 3) { fprintf(stderr, "Aufruf: %s main.elf datei.raw [max_s]\n", argv[0]); return 1; }
    fin = fopen(argv[2], "rb");
    if (!fin) { perror(argv[2]); return 1; }
    if (elf_read_firmware(argv[1], &f)) { fprintf(stderr, "ELF?\n"); return 1; }
    avr = avr_make_mcu_by_name("atmega328p");
    avr_init(avr);
    avr_load_firmware(avr, &f);
    avr->frequency = 16000000;
    avr->avcc = 5000; avr->vcc = 5000;
    adc0 = avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT), uart_out, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0), pb, NULL);
    avr_raise_irq(adc0, 2500);
    avr_cycle_timer_register(avr, 100, feed, NULL);
    while (!eof && avr->cycle < (uint64_t)(maxs * 16e6)) {
        int st = avr_run(avr);
        if (st == cpu_Done || st == cpu_Crashed) break;
    }
    /* Auslauf: Firmware noch den Rest senden lassen */
    { uint64_t e = avr->cycle + 16000000UL / 4; while (avr->cycle < e) avr_run(avr); }
    fflush(stdout);
    fprintf(stderr, "# simavr: %.2f s simuliert, %llu Abtastwerte eingespeist", avr->cycle / 16e6, (unsigned long long)n_fed);
    if (isr_cnt) fprintf(stderr, ", ISR %llu x, Last %.1f %% (mittel %.0f Takte, max %llu Takte von 416,7)",
                         (unsigned long long)isr_cnt, 100.0 * load_sum / (double)(n_fed * 416.667), (double)load_sum / isr_cnt,
                         (unsigned long long)load_max);
    if (getenv("AVR_DEBUG")) fprintf(stderr, "PC=%04x I=%d ADC=%02x%02x ", avr->pc, avr->sreg[7], avr->data[0x79], avr->data[0x78]);
    if (getenv("AVR_DEBUG")) fprintf(stderr, "dump[0x100..]=%02x %02x %02x %02x ", avr->data[0x100], avr->data[0x101], avr->data[0x102], avr->data[0x103]);
    if (getenv("AVR_DEBUG")) fprintf(stderr, "PC=%04x I=%d ADC=%02x%02x ", avr->pc, avr->sreg[7], avr->data[0x79], avr->data[0x78]);
    if (getenv("AVR_DEBUG")) fprintf(stderr, "ADCSRA=%02x ADCSRB=%02x TCCR1B=%02x OCR1A=%02x%02x TCNT1=%02x%02x TIFR1=%02x SREG=%02x\n", avr->data[0x7A], avr->data[0x7B], avr->data[0x81], avr->data[0x89], avr->data[0x88], avr->data[0x85], avr->data[0x84], avr->data[0x36], avr->data[0x5F]);
    fprintf(stderr, "\n");
    return 0;
}
