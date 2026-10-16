#!/usr/bin/env python3
"""WAV -> Rohdatei fuer host_sim (uint16 little endian, 10-Bit-Werte, Abtastrate 9600*OSR).

  NF-Mono (Ausgang eines NFM-Empfaengers):   wav2raw.py in.wav out.raw
  IQ-Stereo (SDR-Aufnahme):                  wav2raw.py in.wav out.raw --iq   (FM-Demodulation hier)
  --osr 4 (Standard, 38,4 kHz, ATmega)  oder  --osr 5 (48 kHz, PC/ESP32)
  --gain G   Verstaerkung/Pegel (Standard: automatisch auf ca. 60 % des ADC-Bereichs)
Die Abtastrate wird aus dem WAV-Kopf gelesen und mit einem Polyphasen-Resampler (mit
Antialias-Tiefpass) exakt auf 9600*OSR umgerechnet.
"""
import sys, wave, argparse
from math import gcd
import numpy as np
from scipy import signal

ap = argparse.ArgumentParser()
ap.add_argument('wav'); ap.add_argument('raw')
ap.add_argument('--iq', action='store_true')
ap.add_argument('--osr', type=int, default=4)
ap.add_argument('--gain', type=float, default=0)
ap.add_argument('--offset', type=float, default=0, help='Gleichanteil (Bruchteil des Bereichs)')
a = ap.parse_args()

w = wave.open(a.wav)
fs, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
raw = w.readframes(w.getnframes())
x = np.frombuffer(raw, np.uint8).astype(float) - 128 if sw == 1 else np.frombuffer(raw, np.int16).astype(float)
x = x.reshape(-1, ch)
if a.iq:
    z = x[:, 0] + 1j * x[:, 1]
    sos = signal.butter(6, 7500, 'low', fs=fs, output='sos')       # Kanalfilter
    z = signal.sosfilt(sos, z.real) + 1j * signal.sosfilt(sos, z.imag)
    x = np.append(np.angle(z[1:] * np.conj(z[:-1])), 0)             # Diskriminator
else:
    x = x[:, 0]
fo = 9600 * a.osr
if fs != fo:
    g = gcd(int(fs), fo)
    x = signal.resample_poly(x, fo // g, int(fs) // g)
sd = np.std(x)
gain = a.gain if a.gain else 0.6 * 512 / (4 * sd)                  # 4 sigma -> 60 % des Halbbereichs
y = np.clip(np.round(512 + a.offset * 1024 + gain * x), 0, 1023).astype('<u2')
y.tofile(a.raw)
print(f"{a.wav}: {fs} Hz -> {fo} Hz, {len(y)/fo:.1f} s, Verstaerkung {gain:.3g}", file=sys.stderr)
