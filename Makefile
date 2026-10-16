TARGET = main
MCU = atmega328p
F_CPU = 16000000UL
PORT = /dev/ttyUSB0
PROGRAMMER = arduino
# Nano mit altem Bootloader: 57600, mit neuem: 115200
BAUD = 57600

SRC = ais_dsp.c ais_msg.c ais_app.c
CC = avr-gcc
OBJCOPY = avr-objcopy
CFLAGS = -Wall -Wextra -Os -DF_CPU=$(F_CPU) -mmcu=$(MCU) -std=gnu99 -flto -ffunction-sections -fdata-sections $(EXTRA)
LDFLAGS = -Wl,--gc-sections

all: $(TARGET).hex size

$(TARGET).elf: main.c $(SRC) ais_dsp.h ais_msg.h ais_app.h ais_port.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c $(SRC)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	@avr-size -C --mcu=$(MCU) $(TARGET).elf 2>/dev/null || avr-size $(TARGET).elf

flash: $(TARGET).hex
	avrdude -F -V -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$(TARGET).hex:i

# Monitor mit Eingabe (Befehle n d a b s ?): picocom, sonst screen. Beenden: picocom Strg-A Strg-X, screen Strg-A K
monitor:
	@if command -v picocom >/dev/null; then picocom -b 38400 --omap crlf --imap lfcrlf $(PORT); \
	else screen $(PORT) 38400; fi

# Nur lesen (kann nichts senden):  make monitor_ro
monitor_ro:
	stty -F $(PORT) 38400 cs8 -cstopb -parenb raw clocal -echo
	cat $(PORT)

# Einzelnen Befehl senden:  make cmd C=d   (d = Klartext an/aus, n = NMEA an/aus, s = Statistik)
cmd:
	stty -F $(PORT) 38400 cs8 -cstopb -parenb raw clocal -echo
	@printf '%s' '$(C)' > $(PORT)

# ---- PC-Simulation: derselbe Kern (ais_dsp.c / ais_msg.c / ais_app.c), Abtastwerte aus Datei ----
#   make test RAW=ais.raw      (uint16 little endian, 10 Bit, 38,4 kHz; siehe tools/wav2raw.py)
RAW ?= ais.raw
OSR ?= 4
.PHONY: host_sim clean_fw
host_sim: host_sim.c $(SRC) ais_dsp.h ais_msg.h ais_app.h ais_port.h
	gcc -O2 -Wall -Wextra -std=gnu99 -DAIS_OSR=$(OSR) -o $@ host_sim.c $(SRC)

test: host_sim
	./host_sim $(RAW)

testall: host_sim
	python3 tools/test_all.py

# Aufnahme -> Rohdatei:  make raw WAV=testsignal/ais_nf.wav RAW=ais.raw [IQ=--iq]
raw:
	python3 tools/wav2raw.py $(WAV) $(RAW) $(IQ)

clean: clean_fw
	rm -f avr_sim
	rm -f host_sim

# ---- Echte Firmware im AVR-Simulator (simavr): make avr_sim; ./avr_sim main.elf ais.raw ----
avr_sim: tools/avr_sim.c
	gcc -O2 -Wall -o $@ $< -lsimavr -lelf -lm

# Firmware mit Lastmessung (PB0) bauen und im Simulator laufen lassen
simload: avr_sim
	$(MAKE) clean_fw
	$(MAKE) main.elf EXTRA="-DDEBUG_LOAD -DAIS_NO_RX -DAIS_SIM"
	./avr_sim main.elf $(RAW) $(SECS)

clean_fw:
	rm -f main.elf main.hex
