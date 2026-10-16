# AIS decoder for the ATmega328 (bare metal)

A complete **AIS receiver decoder** in portable C: the audio output of an NFM scanner goes straight into the ADC of an ATmega328, and
the UART delivers **`!AIVDM` NMEA sentences** – optionally every message (all types 1–27) as formatted plain text.
No Arduino core, no floating point, no PLL, no I/Q. The same core runs on a PC (`host_sim`), in the AVR simulator (simavr) and – unchanged – on an ESP32.

![Signal chain](doc/signalkette.svg)

*(Deutsche Version: [README.de.md](README.de.md))*   *Use case OpenCPN: [OpenCPN.en.md](OpenCPN.en.md)*

---

## Contents
1. [What is AIS?](#1-what-is-ais)
2. [The protocol, layer by layer](#2-the-protocol-layer-by-layer)
3. [The messages](#3-the-messages)
4. [NMEA output (`!AIVDM`)](#4-nmea-output-aivdm)
5. [The receiver: how the code works](#5-the-receiver-how-the-code-works)
6. [Hardware, build, operation](#6-hardware-build-operation)
7. [Tests and results](#7-tests-and-results)
8. [Known limits](#8-known-limits)
9. [Porting (ESP32 / ESP-IDF)](#9-porting-esp32--esp-idf)
10. [File overview](#10-file-overview)

(The figures contain German labels; the captions below explain them.)

---

## 1. What is AIS?

The **Automatic Identification System** is a maritime radio system (IMO/SOLAS, ITU-R M.1371-5): ships automatically broadcast their identity (MMSI),
position, course and speed; other ships and shore stations receive it and know who is nearby – without radar or voice radio. There are two device classes:

| | Class A (commercial) | Class B (leisure craft) |
|---|---|---|
| Transmit power | 12.5 W | 2 W (CS) / 5 W (SO) |
| Position report interval | 2–10 s under way, 3 min at anchor | 30 s (3 min below 2 kn) |
| Static data (name, destination ...) | type 5, every 6 min | type 24, every 6 min |
| Message types | 1, 2, 3, 5, ... | 18, 19, 24 |

**Shore stations** (base stations, type 4), **aids to navigation** (AtoN, type 21), **SAR aircraft** (type 9) and emergency beacons (AIS-SART, MOB, EPIRB – recognisable by their MMSI) transmit AIS too.

**Radio parameters:** two VHF channels, **AIS 1 = 161.975 MHz (channel 87B, "A")** and **AIS 2 = 162.025 MHz (channel 88B, "B")**, 25 kHz channel spacing,
typical range 20–40 nautical miles. Each channel is divided into **2250 time slots per minute**, one slot lasts **26.67 ms** (= 256 bit at 9600 bit/s). The devices synchronise to GNSS time and
organise slots themselves (SOTDMA/ITDMA for class A, CSTDMA for class B) – no master. A receiver therefore only has to listen and detect bursts without knowing the slot timing.

---

## 2. The protocol, layer by layer

### 2.1 Physical layer: GMSK, 9600 bit/s
The bits switch the carrier frequency by **±2.4 kHz** (FSK, modulation index 0.5), after a **Gaussian filter (BT = 0.4)**: the edges are rounded and the spectrum stays narrow (**GMSK**).
The price is a small **inter-symbol interference**: every bit is slightly influenced by its neighbour. A narrow-band FM receiver (NFM, 25 kHz) delivers exactly this voltage at its audio output:
+2.4 kHz deviation = high, −2.4 kHz = low. That is the input of this project.

### 2.2 Link layer: HDLC frame, bit stuffing, NRZI, CRC
![HDLC frame, bit stuffing, NRZI](doc/rahmen.svg)

*Figure: frame = Training (24 bit `0101…`), Flag, payload ("Nutzdaten"), FCS, Flag, buffer ("Puffer"). Bit stuffing: a 0 is inserted after five 1s (red). NRZI: bit 0 = level change, bit 1 = level stays.*

* **Frame:** 24-bit training sequence `0101…` (receiver settling), start flag `01111110` (0x7E), payload, 16-bit **FCS**, end flag.
* **Bit stuffing:** so that the flag never occurs inside the data, the sender inserts a 0 after every fifth 1; the receiver removes it.
* **NRZI:** a **0** is sent as a **level change**, a **1** as **"level stays"**. Only the change matters – polarity and DC offset (crystal frequency error) are irrelevant to the receiver. This is why
  **no carrier tracking** is needed here.
* **CRC-16/X.25:** polynomial 0x1021 (reflected 0x8408), initial value 0xFFFF, result inverted. Computed over the whole frame, an error-free reception leaves the residue **0xF0B8**. This is the only error detection;
  that is why the sentences output are "100 % consistent".
* **Bit order (the classic pitfall):** on air, each octet is sent **least significant bit first** (HDLC). The message fields are laid out **MSB first**. The decoder stores bits in air order and reads fields with
  `NMEA bit i = bit (7 − i mod 8) of byte (i div 8)`.

A real burst next to a clean simulated one (training shows as a 2.4 kHz oscillation; the flag with six equal bits as a flat part):

![AIS burst](doc/burst.png)

*Top: simulated, clean. Bottom: real recording (Skagen), same structure with noise and fading.*

### 2.3 Messages
The payload is a bit string of fixed-width fields (MSB first). The first 6 bits are the **message type** (1–27), then 2 bits repeat count and the 30-bit **MMSI** (9-digit ship identity; the first three digits are the country code MID, e.g. 211 = Germany, 366 = USA).

---

## 3. The messages

![Message types](doc/nachrichtentypen.svg)

*Columns: type, content, length in bit, transmission interval.*

**All 27 types are decoded** – passed on raw in NMEA mode, explained field by field in plain-text mode. The text decoding is checked field by field against the independent Python library *pyais* (see [tests](#7-tests-and-results)).

### Type 1/2/3 – position report, class A
![Type 1](doc/typ1.svg)

*Fields: type, repeat, MMSI, nav status, rate of turn, SOG, accuracy, longitude, latitude, COG, heading, UTC second, manoeuvre, spare, RAIM, radio status.*

Number formats: latitude/longitude in **1/10000 minute** (÷ 600000 = degrees, signed), SOG in **1/10 kn**, COG in **1/10°**, rate of turn in a square-root encoding. "Not available": lat 91°, lon 181°, SOG 1023, COG 3600, heading 511, ROT −128, UTC second 60.

### Type 5 – static and voyage data (2 slots)
![Type 5](doc/typ5.svg)

*Fields: ..., AIS version, IMO number, call sign, name, ship type, dimensions, EPFD type, ETA, draught, destination, DTE.*

Name, call sign and destination are **6-bit ASCII** (0 = `@` = padding, 1–31 = `A`…`_`, 32–63 = space…`?`); dimensions are distances of the GNSS antenna to bow, stern, port, starboard.

### Type 18 – position report, class B
![Type 18](doc/typ18.svg)

### Type 8 – binary broadcast (and 6, 25, 26)
![Type 8](doc/typ8.svg)

**DAC** (designated area code, essentially a country code) and **FI** (functional ID) define the application, e.g. DAC 1 / FI 0 = text, DAC 200 / FI 10 = inland shipping. The decoder shows DAC/FI and the data as a hex dump; DAC 1 / FI 0 is printed as text.

---

## 4. NMEA output (`!AIVDM`)

![NMEA](doc/nmea.svg)

*Fields: talker/format, number of parts, part number, sequence ID, channel, payload (6-bit ASCII armour), fill bits, checksum.*

* A sentence holds **60 payload characters**; longer messages (e.g. type 5: 424 bit = 71 characters) are split into **2–3 sentences** sharing a **sequence ID (0–9)** that the decoder assigns round-robin.
* Channel `A` or `B` is selectable by command (`a` / `b`); the decoder cannot know it from the signal.
* The trailing `*hh` is the XOR checksum (two hex digits) of all characters between `!` and `*`.
* The format matches a hardware AIS receiver (NMEA 0183, 38400 baud) and can be fed to e.g. OpenCPN, gpsd or `pyais`.

Example (type 5 in two sentences, then type 1):
```
!AIVDM,2,1,0,A,55Mw>bP00001L<C;7O4h58<tp62222222222220P1H?624SosFS<4p0CQiC1,0*30
!AIVDM,2,2,0,A,Dp888888888,2*18
!AIVDM,1,1,,A,15MqDMPP00GRo;RCDf`@0?vT0@;W,0*74
```

The same messages in **plain-text debug mode** (`d`; the text output is in German – "Pegel" = eye opening of the received bit phase, "grob" = position accuracy flag 0, worse than 10 m):
```
[A] Pegel 52  Typ 5  Statische und Reisedaten Klasse A
    MMSI 366988970 (MID 366)  Repeat 0  (424 Bit)
    AIS-Version 0  IMO 0  Rufzeichen "WCD2171"
    Name "LARCONA"  Typ 32 Schlepper gross
    Abmessungen Bug 11 Heck 15 Backbord 6 Steuerbord 2 m  EPFD GPS
    ETA 02-07 23:59  Tiefgang 9.0 m
    Ziel "L0S ANGELES"  DTE nicht bereit
[A] Pegel 53  Typ 1  Positionsmeldung Klasse A (planmaessig)
    MMSI 366892150 (MID 366)  Repeat 0  (168 Bit)
    Status 0 unter Maschine  ROT n.v.
    SOG 0.0 kn  COG 0.0°  HDG n.v.
    Pos 33°46.2561'N  118°12.8015'W (grob)
    UTC-Sekunde 18  Manoever n.v.  RAIM nein
    Funk: SOTDMA Sync 0 Timeout 4 Sub 743
```

---

## 5. The receiver: how the code works

The core (`ais_dsp.c`, about 100 lines without comments) is commented as a teaching example. In short:

1. **The input is a discriminator output, not I/Q.** An NFM scanner has already done the FM demodulation, so mixing, carrier tracking and frequency-offset estimation are not needed. (For IQ recordings `tools/wav2raw.py --iq` first computes the discriminator `Im{z[n]·conj(z[n−1])}`.)
2. **4 samples per bit (38.4 kHz).** On the ATmega, Timer1 generates the sampling instants exactly (period 416/417/417 cycles = 38.400 kHz on average) and triggers the ADC by hardware – no ISR jitter. At 48 kHz (PC/ESP32) use `AIS_OSR=5`.
3. **Integrate & dump:** the sum of the last 4 samples (exactly one bit time) is a low-pass filter and at the same time the optimum filter for a rectangular bit.
4. **Threshold without a fixed zero:** per bit phase, two trackers follow the mean high and low level; the threshold is their midpoint. The DC offset (frequency error up to ±1 kHz, ADC offset) drops out automatically. There is **no** assumed "512 = zero"; only the trackers' start value is mid-scale.
5. **No PLL – phase diversity:** the sampling phase relative to the sender's bit clock is unknown at the start of a burst, and 24 training bits are too few to lock a PLL. Therefore **each of the 4 sampling phases is a complete receiver** (decision, NRZI, flag search, destuffing, frame buffer). Only one computes per sample – the load is that of a single one. Phases near the ideal instant deliver a
   correct frame, the others garbage (wrong CRC, discarded). Duplicates are removed in `ais_app.c` by length, FCS and time distance.
6. **Short interrupt routine:** the ISR only shifts bits into frame buffers. CRC, NMEA and text are done in the main loop.
7. **Plausibility check (`AIS_STRICT`):** type 1–27 and a length table per M.1371-5; unknown types and illegal lengths are dropped even if the CRC is right.

Resources (avr-gcc, `-Os`, `-flto`): flash **17.6 kB** (54 %), RAM **954 bytes** (47 %); the ISR needs on average **199**, at most **363 of 416 cycles** in the simulator (48 % load). The main loop (CRC, UART output) runs concurrently because the ISR keeps running.

---

## 6. Hardware, build, operation

**Input circuit** (audio straight behind the FM discriminator – not the loudspeaker output with a voice filter!):
```
                      Vcc
                       |
                      [10k]
   audio ---||---+------+------[1k]------+---- ADC0 (A0)
        100 nF   |                       |
                [10k]                  [22n]
                 |                       |
                GND                     GND
```
Bias at Vcc/2, anti-alias low-pass about 7 kHz. The AIS deviation (±2.4 kHz) should give at least about ±50 LSB at the ADC without clipping.
The 38.4 kHz sampling rate = 4 × 9600 comes from Timer1 and the 16 MHz crystal. UART: **38400 8N1**. The LED on PB5 flashes on every valid message.

**Build**
```
make                       # firmware main.hex (avr-gcc); make flash programs it (avrdude)
make host_sim OSR=4        # PC version:  ./host_sim file.raw [-t text] [-b channel B] [-q no statistics]
make raw WAV=recording.wav RAW=x.raw [IQ=--iq]
make testall               # full test, writes testsignal/testergebnis.txt
make simload RAW=x.raw SECS=20   # real firmware in simavr, with load measurement
make monitor               # serial terminal with input (picocom/screen); make cmd C=d sends one character
make monitor_ro            # just reading serial terminal, no input
```
**UART commands** (one character): `n` NMEA on/off · `d` plain text on/off · `a`/`b` channel · `s` statistics · `?` help.

---

## 7. Tests and results

`make testall` (`tools/test_all.py`) runs everything (result: [testsignal/testergebnis.txt](testsignal/testergebnis.txt)):

| Case | Expected | Decoded | Remark |
|---|---:|---:|---|
| Skagen, IQ recording (OSR 4 and 5) | 13 | 13 / 13 | independent reference decoder (Python) |
| Algarve, audio recording | 8 | 8 / 8 | + 2 further CRC-valid messages |
| Los Angeles test 1 (8 bit, 48 kHz) | 77 | 71 / 74 | regression reference (OSR 4 / 5) |
| Los Angeles test 2 (16 bit, 48 kHz) | 112 | 109 / 111 | 5 types (1, 3, 5, 10, 11) incl. a 2-sentence type 5; all 105 messages of the OSR-4 reference decodable by pyais |
| synthetic, clean (60 messages, all types) | 61 | 60 | |
| synthetic, fading 12 dB / ±50 ppm / 30 Hz HP / 800 Hz DC | 61 | 58–60 | |
| synthetic, 14 dB SNR | 61 | 27–29 | noise limit |
| text decoder, all types 1–27 | 1187 field checks | 0 deviations | against *pyais* |

Further tools: `tools/gen_ais.py` (bit-exact test-signal generator, all types, GMSK, noise, fading, DC, high-pass), `tools/ref_decoder.py` (independent floating-point decoder in Python), `tools/compare.py`, `tools/test_text.py`, `tools/avr_sim.c` (simavr harness for the real firmware), `tools/make_doc.py` (creates the figures in `doc/`).

---

## 8. Known limits

* **Voice-channel audio does not work.** If the audio is filtered to 300–3700 Hz (e.g. KiwiSDR in NFM mode, MP3 recordings, the loudspeaker output of many scanners), the components carrying the 9600 bit/s are missing, and a 300 Hz high-pass lets long equal-level runs "droop". Simulating that frequency response: 0 of 43 messages. Remedy: tap the **discriminator output** or **record IQ** and use `wav2raw.py --iq`.
* **DC offset:** frequency errors up to about ±1.5 kHz are absorbed; more degrades reception.
* **Noise:** below about 15 dB SNR the rate drops sharply (see table); errors are always rejected by the CRC, never output wrongly.
* **Slot collisions:** two simultaneous bursts cannot be separated; there is no multi-frame reception.
* Hardware test: the recordings were played back into a real ATmega328 (ADC input, Timer1 auto-trigger, ISR, UART) and decoded; the simavr run (substitute variant `-DAIS_SIM`) is only for load measurement and regression.

---

## 9. Porting (ESP32 / ESP-IDF)

Not needed for this task – the ATmega328 suffices (and is 54 % full). The core is platform-neutral though: `ais_dsp.c`, `ais_msg.c`, `ais_app.c` know neither ADC nor UART.
For the ESP32 (ESP-IDF 6.1) replace only `main.c`: sample the ADC/I²S at 48 kHz, pass each 10-bit value (12 bit → `>> 2`) to `ais_dsp_sample()`, call `ais_app_poll()` from a task and map `ais_putc()` to `uart_write_bytes()`; compile with `-DAIS_OSR=5`. On non-AVR targets the flash access macro (`AIS_ROM`) is an ordinary memory access.

---

## 10. File overview

| File | Content |
|---|---|
| `ais_port.h` | portability layer (PROGMEM, `ais_putc`) |
| `ais_dsp.c/.h` | receiver core: integrate & dump, threshold, NRZI, flag, destuffing, frame buffers (ISR-suitable) |
| `ais_msg.c/.h` | CRC, type/length check, NMEA output, plain text for all types |
| `ais_app.c/.h` | main loop: check frames, remove duplicates, output, commands, statistics |
| `main.c` | ATmega328P: Timer1/ADC sampling, UART, LED |
| `host_sim.c` | PC test harness (raw file → decoder), NMEA→text mode |
| `Makefile` | firmware, PC simulation, tests, simavr |
| `tools/` | generator, reference decoder, test scripts |
| `testsignal/` | recordings, references, decoded outputs, test result |
| `doc/` | figures for this README |

*Sources: ITU-R M.1371-5 (AIS technical characteristics), IEC 61162-1 / NMEA 0183 (`!AIVDM`), IEC 62287 (class B CS).*
