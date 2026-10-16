#!/usr/bin/env python3
"""Unabhaengiger Referenzdecoder (Gleitkomma, Python/numpy) fuer AIS-Signale.

Eingabe: WAV (IQ stereo 16 Bit  -> --iq,  oder NF-Mono 8/16 Bit hinter einem FM-Diskriminator).
Ausgabe: gueltige HDLC-Rahmen als !AIVDM-Zeilen (ein Teil je Zeile), zusaetzlich die Burst-Zeit.
Anderer Algorithmus als main.c: Burst-Erkennung per Huellkurve/Energie, alle Bitphasen
durchprobiert (kein PLL), Bitentscheidung per Mittelwert ueber das Bit, Flag-Suche, CRC.
"""
import sys, wave, argparse
import numpy as np
from scipy import signal

def read_wav(fn):
    w = wave.open(fn)
    n, ch, sw, fs = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if sw == 1:
        a = np.frombuffer(raw, np.uint8).astype(float) - 128
    elif sw == 2:
        a = np.frombuffer(raw, np.int16).astype(float)
    else:
        raise SystemExit("Samplebreite?")
    return a.reshape(-1, ch), fs

def check_crc(bits):
    # Rahmen inkl. FCS: Restwert 0xF0B8 (vor End-Xor) <=> 0x0F47 danach
    crc = 0xFFFF
    for b in bits:
        fb = (crc ^ b) & 1
        crc >>= 1
        if fb: crc ^= 0x8408
    return crc == 0xF0B8

def frames_from_bits(bits):
    """NRZI schon decodiert; sucht 0x7E (LSB-first 01111110), destuffed, prueft CRC."""
    out = []
    n = len(bits)
    flag = [0,1,1,1,1,1,1,0]
    i = 0
    starts = [k for k in range(n-8) if list(bits[k:k+8]) == flag]
    for s in starts:
        j = s+8; ones = 0; payload = []
        while j < n:
            b = bits[j]; j += 1
            if b: 
                ones += 1
                if ones > 6: break
                payload.append(1)
            else:
                if ones == 5: ones = 0; continue    # Stuffbit
                if ones == 6:                       # Endflag
                    payload = payload[:-7]
                    if len(payload) >= 56 and check_crc(payload) and len(payload) % 8 == 0:
                        out.append(payload[:-16])
                    break
                ones = 0; payload.append(0)
    return out

def nmea(bits, chan='A'):
    # Luftschnittstelle: je Oktett LSB zuerst; die NMEA-Bitfolge hat dagegen MSB zuerst -> Oktette spiegeln
    bits = list(bits)
    assert len(bits) % 8 == 0
    bits = [b for k in range(0, len(bits), 8) for b in bits[k:k+8][::-1]]
    n = len(bits); fill = (-n) % 6
    b = list(bits) + [0]*fill
    s = ''
    for k in range(0, len(b), 6):
        v = 0
        for x in b[k:k+6]: v = v*2+x
        v += 48
        if v > 87: v += 8
        s += chr(v)
    lines = []
    parts = [s[i:i+60] for i in range(0, len(s), 60)] or ['']
    for idx, p in enumerate(parts):
        f = fill if idx == len(parts)-1 else 0
        seq = '1' if len(parts) > 1 else ''
        body = f"AIVDM,{len(parts)},{idx+1},{seq},{chan},{p},{f}"
        cs = 0
        for c in body: cs ^= ord(c)
        lines.append(f"!{body}*{cs:02X}")
    return lines

def decode(x, fs, is_iq, chan='A'):
    if is_iq:
        z = x[:,0] + 1j*x[:,1]
        sos = signal.butter(6, 7500, 'low', fs=fs, output='sos')      # Kanalfilter
        z = signal.sosfilt(sos, z.real) + 1j*signal.sosfilt(sos, z.imag)
        d = np.angle(z[1:]*np.conj(z[:-1])); d = np.append(d, 0)
    else:
        d = x[:,0].copy()
    sos = signal.butter(4, 5500, 'low', fs=fs, output='sos')
    d = signal.sosfilt(sos, d)
    sos = signal.butter(1, 30, 'high', fs=fs, output='sos')           # DC / Traegerablage raus
    d = signal.sosfilt(sos, d)
    spb = fs/9600.0
    cs = np.concatenate(([0.0], np.cumsum(d)))
    res = []; seen = set()
    for ph in np.arange(0, spb, 0.5):
        a = np.round(np.arange(ph, len(d)-spb-1, spb)).astype(int)
        b = np.round(np.arange(ph, len(d)-spb-1, spb)+spb).astype(int)
        soft = cs[b]-cs[a]
        for pol in (1, -1):
            raw = (pol*soft > 0).astype(np.int8)
            bits = (1 - (raw[1:] ^ raw[:-1])).astype(np.int8)          # NRZI: gleich = 1
            # Flag 01111110 per Fenstervergleich
            m = np.ones(len(bits)-8, bool)
            pat = [0,1,1,1,1,1,1,0]
            for k in range(8): m &= (bits[k:len(bits)-8+k] == pat[k])
            for s0 in np.nonzero(m)[0]:
                seg = bits[s0:s0+8+1100]
                for fr in frames_from_bits(seg):
                    key = tuple(fr)
                    if key in seen: continue
                    seen.add(key)
                    res.append((a[s0]/fs, fr))
    return res

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('wav'); ap.add_argument('--iq', action='store_true')
    ap.add_argument('--chan', default='A')
    a = ap.parse_args()
    x, fs = read_wav(a.wav)
    res = decode(x, fs, a.iq, a.chan)
    res.sort(key=lambda r: r[0])
    for t, fr in res:
        for l in nmea(fr, a.chan):
            print(l)
    print(f"# {len(res)} gueltige Rahmen", file=sys.stderr)
