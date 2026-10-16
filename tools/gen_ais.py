#!/usr/bin/env python3
"""Synthetisches AIS-Signal (GMSK, 9600 Bit/s) hinter einem NFM-Diskriminator + Wahrheitsliste.

  gen_ais.py out.wav [--msgs N] [--types 1,5,8,...|all] [--snr dB] [--dc Hz] [--hp Hz]
                     [--ppm P] [--audio-bw Hz] [--fs 48000] [--seed S] [--fade dB] [--dur s]
Schreibt  out.wav (Mono, 16 Bit) und  out.truth.txt (die gesendeten !AIVDM-Saetze).
Die Nachrichten werden Bit fuer Bit nach ITU-R M.1371-5 gebaut (alle Typen 1..27) und
HDLC-gerahmt (Training, Flag, Stopfbits, CRC, Flag).  Kanalmodell: GMSK BT=0,4 / Hub 2,4 kHz,
Frequenzablage (--dc), AWGN im 15-kHz-Zwischenfrequenzfilter, Diskriminator (ergibt das
typische "Klick"-Rauschen unter dem Schwellwert), NF-Tiefpass, optional Hochpass (AC-Kopplung).
"""
import argparse, math, random, sys
import numpy as np
from scipy import signal

# ---------------------------------------------------------------- Bitaufbau
class B:
    def __init__(s): s.b = []
    def u(s, v, n):
        v = int(v) & ((1 << n) - 1)
        s.b += [(v >> (n - 1 - i)) & 1 for i in range(n)]
        return s
    def txt(s, t, nch):
        t = t.upper()[:nch].ljust(nch, '@')
        for c in t:
            v = ord(c)
            s.u(v - 64 if v >= 64 else v, 6)
        return s
    def pad(s, n): return s.u(0, n)
    def __len__(s): return len(s.b)

def lonlat(x, scale): return int(round(x * scale))

def gen_msg(t, r):
    """Zufaellige, plausible Nachricht des Typs t -> Liste von Bits (NMEA-Reihenfolge)."""
    b = B(); mmsi = r.randint(200000000, 799999999)
    b.u(t, 6).u(r.choice([0, 0, 0, 1, 2, 3]), 2).u(mmsi, 30)
    lon = r.uniform(-180, 180) if r.random() < .3 else r.uniform(5, 15)
    lat = r.uniform(-85, 85) if r.random() < .3 else r.uniform(50, 58)
    L28, A27 = lonlat(lon, 600000), lonlat(lat, 600000)
    if t in (1, 2, 3):
        b.u(r.randint(0, 15), 4).u(r.randint(-127, 127), 8).u(r.randint(0, 1022), 10).u(r.randint(0, 1), 1)
        b.u(L28, 28).u(A27, 27).u(r.randint(0, 3599), 12).u(r.randint(0, 359), 9).u(r.randint(0, 63), 6)
        b.u(r.randint(0, 2), 2).u(0, 3).u(r.randint(0, 1), 1).u(r.getrandbits(19), 19)
    elif t in (4, 11):
        b.u(r.randint(2020, 2030), 14).u(r.randint(1, 12), 4).u(r.randint(1, 28), 5).u(r.randint(0, 23), 5)
        b.u(r.randint(0, 59), 6).u(r.randint(0, 59), 6).u(r.randint(0, 1), 1).u(L28, 28).u(A27, 27)
        b.u(r.choice([1, 2, 3, 7, 8, 15]), 4).u(0, 10).u(0, 1).u(r.getrandbits(19), 19)
    elif t == 5:
        b.u(0, 2).u(r.randint(1000000, 9999999), 30).txt('D%s' % r.randint(1000, 9999), 7)
        b.txt(r.choice(['NORDSTERN', 'MS TEST SCHIFF', 'ELBE EXPRESS', 'A B C']), 20).u(r.choice([30, 52, 60, 70, 79, 89, 99]), 8)
        b.u(r.randint(0, 300), 9).u(r.randint(0, 100), 9).u(r.randint(0, 30), 6).u(r.randint(0, 30), 6).u(r.choice([1, 2, 3]), 4)
        b.u(r.randint(1, 12), 4).u(r.randint(1, 28), 5).u(r.randint(0, 23), 5).u(r.randint(0, 59), 6).u(r.randint(5, 150), 8)
        b.txt(r.choice(['HAMBURG', 'ROTTERDAM >> ANTWERP', 'SKAGEN']), 20).u(0, 1).u(0, 1)
    elif t == 6:
        b.u(r.randint(0, 3), 2).u(r.randint(200000000, 799999999), 30).u(r.randint(0, 1), 1).u(0, 1)
        b.u(r.choice([1, 235, 211]), 10).u(r.randint(0, 63), 6)
        for _ in range(r.randint(0, 5) * 8 + r.choice([0, 8, 16])): b.u(r.getrandbits(1), 1)
    elif t in (7, 13):
        b.u(0, 2)
        for i in range(r.randint(1, 4)): b.u(r.randint(200000000, 799999999), 30).u(r.randint(0, 3), 2)
    elif t == 8:
        b.u(0, 2).u(r.choice([1, 200, 235]), 10).u(r.randint(0, 63), 6)
        for _ in range(r.randint(0, 119)): b.u(r.getrandbits(8), 8)
    elif t == 9:
        b.u(r.randint(0, 4095), 12).u(r.randint(0, 1023), 10).u(1, 1).u(L28, 28).u(A27, 27).u(r.randint(0, 3599), 12)
        b.u(r.randint(0, 63), 6).u(0, 8).u(0, 1).u(0, 3).u(0, 1).u(0, 1).u(r.getrandbits(20), 20)
    elif t == 10:
        b.u(0, 2).u(r.randint(200000000, 799999999), 30).u(0, 2)
    elif t == 12:
        b.u(r.randint(0, 3), 2).u(r.randint(200000000, 799999999), 30).u(0, 1).u(0, 1)
        b.txt(r.choice(['HELLO WORLD', 'CHECK RADIO CHANNEL 16', 'TEST 123 ABC']), r.randint(4, 30))
    elif t == 14:
        b.u(0, 2).txt(r.choice(['SECURITE SECURITE', 'MAN OVERBOARD', 'BUOY ADRIFT 54N 008E']), r.randint(4, 30))
    elif t == 15:
        b.u(0, 2).u(r.randint(200000000, 799999999), 30).u(r.randint(1, 27), 6).u(r.randint(0, 4095), 12)
        n = r.choice([88, 110, 160])
        if n >= 110:
            b.u(0, 2).u(r.randint(1, 27), 6).u(r.randint(0, 4095), 12).u(0, 2)
        if n >= 160:
            b.u(r.randint(200000000, 799999999), 30).u(r.randint(1, 27), 6).u(r.randint(0, 4095), 12).u(0, 2)
    elif t == 16:
        b.u(0, 2).u(r.randint(200000000, 799999999), 30).u(r.randint(0, 4095), 12).u(r.randint(0, 1023), 10)
        if r.random() < .5:
            b.u(r.randint(200000000, 799999999), 30).u(r.randint(0, 4095), 12).u(r.randint(0, 1023), 10)
    elif t == 17:
        b.u(0, 2).u(lonlat(lon, 600), 18).u(lonlat(lat, 600), 17).u(0, 5)
        for _ in range(r.randint(8, 40)): b.u(r.getrandbits(8), 8)
    elif t == 18:
        b.u(0, 8).u(r.randint(0, 1022), 10).u(1, 1).u(L28, 28).u(A27, 27).u(r.randint(0, 3599), 12).u(r.randint(0, 359), 9)
        b.u(r.randint(0, 59), 6).u(0, 2).u(1, 1).u(1, 1).u(0, 1).u(1, 1).u(0, 1).u(0, 1).u(1, 1).u(r.getrandbits(20), 20)
    elif t == 19:
        b.u(0, 8).u(r.randint(0, 1022), 10).u(0, 1).u(L28, 28).u(A27, 27).u(r.randint(0, 3599), 12).u(r.randint(0, 359), 9)
        b.u(r.randint(0, 59), 6).u(0, 4).txt('SPORTBOOT 7', 20).u(37, 8).u(r.randint(0, 30), 9).u(r.randint(0, 10), 9)
        b.u(2, 6).u(2, 6).u(1, 4).u(0, 1).u(0, 1).u(0, 1).u(0, 4)
    elif t == 20:
        b.u(0, 2)
        for i in range(r.randint(1, 4)): b.u(r.randint(1, 2000), 12).u(r.randint(1, 15), 4).u(r.randint(0, 7), 3).u(r.randint(0, 2000), 11)
        b.u(0, (8 - len(b) % 8) % 8)
    elif t == 21:
        b.u(r.randint(1, 31), 5).txt('BOJE ' + str(r.randint(1, 99)), 20).u(1, 1).u(L28, 28).u(A27, 27)
        b.u(r.randint(0, 5), 9).u(r.randint(0, 5), 9).u(1, 6).u(1, 6).u(r.choice([1, 7]), 4).u(r.randint(0, 59), 6)
        b.u(r.randint(0, 1), 1).u(0, 8).u(0, 1).u(r.randint(0, 1), 1).u(0, 1).u(0, 1)
        if r.random() < .5: b.txt('EXTRA NAME', 10); b.pad((8 - len(b) % 8) % 8)
    elif t == 22:
        b.u(0, 2).u(r.randint(2000, 2100), 12).u(r.randint(2000, 2100), 12).u(r.randint(0, 5), 4).u(r.randint(0, 1), 1)
        ad = r.randint(0, 1)
        if ad: b.u(r.randint(200000000, 799999999), 30).u(0, 5).u(r.randint(200000000, 799999999), 30).u(0, 5)
        else: b.u(lonlat(lon + 1, 600), 18).u(lonlat(lat + 1, 600), 17).u(lonlat(lon - 1, 600), 18).u(lonlat(lat - 1, 600), 17)
        b.u(ad, 1).u(r.randint(0, 1), 1).u(r.randint(0, 1), 1).u(r.randint(0, 7), 3).u(0, 23)
    elif t == 23:
        b.u(0, 2).u(lonlat(lon + 1, 600), 18).u(lonlat(lat + 1, 600), 17).u(lonlat(lon - 1, 600), 18).u(lonlat(lat - 1, 600), 17)
        b.u(r.randint(0, 9), 4).u(r.randint(0, 99), 8).u(0, 22).u(r.randint(0, 3), 2).u(r.randint(0, 11), 4).u(r.randint(0, 15), 4).u(0, 6)
    elif t == 24:
        if r.random() < .5:
            b.u(0, 2).txt('KLASSE B BOOT', 20).u(0, 8)
            del b.b[160:]
        else:
            b.u(1, 2).u(37, 8).txt('ABC', 3).u(r.randint(0, 15), 4).u(r.randint(0, 999999), 20).txt('DBCD', 7)
            b.u(r.randint(0, 20), 9).u(r.randint(0, 5), 9).u(r.randint(0, 5), 6).u(r.randint(0, 5), 6).u(0, 6)
    elif t == 25:
        ad, st = r.randint(0, 1), r.randint(0, 1)
        b.u(ad, 1).u(st, 1)
        if ad: b.u(r.randint(200000000, 799999999), 30)
        if st: b.u(1, 10).u(r.randint(0, 63), 6)
        for _ in range(r.randint(1, 8)): b.u(r.getrandbits(8), 8)
    elif t == 26:
        ad, st = r.randint(0, 1), r.randint(0, 1)
        b.u(ad, 1).u(st, 1)
        if ad: b.u(r.randint(200000000, 799999999), 30)
        if st: b.u(1, 10).u(r.randint(0, 63), 6)
        for _ in range(r.randint(2, 40)): b.u(r.getrandbits(8), 8)
        b.u(r.getrandbits(20), 20)
    elif t == 27:
        b.u(r.randint(0, 1), 1).u(0, 1).u(r.randint(0, 15), 4).u(lonlat(lon, 600), 18).u(lonlat(lat, 600), 17)
        b.u(r.randint(0, 63), 6).u(r.randint(0, 359), 9).u(r.randint(0, 1), 1).u(0, 1)
    else:
        raise ValueError(t)
    bits = b.b
    # Laengen auf ganze Oktette auffuellen (HDLC-Rahmen sind byteweise)
    bits += [0] * ((-len(bits)) % 8)
    return bits

def to_nmea(bits, chan='A', seq=0):
    fill = (-len(bits)) % 6
    bb = bits + [0] * fill
    s = ''
    for k in range(0, len(bb), 6):
        v = 0
        for x in bb[k:k + 6]: v = v * 2 + x
        v += 48
        if v > 87: v += 8
        s += chr(v)
    parts = [s[i:i + 60] for i in range(0, len(s), 60)]
    out = []
    for i, p in enumerate(parts):
        f = fill if i == len(parts) - 1 else 0
        body = 'AIVDM,%d,%d,%s,%s,%s,%d' % (len(parts), i + 1, (str(seq) if len(parts) > 1 else ''), chan, p, f)
        cs = 0
        for c in body: cs ^= ord(c)
        out.append('!%s*%02X' % (body, cs))
    return out

# ---------------------------------------------------------------- HDLC / GMSK
def crc_bits(bits):
    crc = 0xFFFF
    for b in bits:
        fb = (crc ^ b) & 1
        crc >>= 1
        if fb: crc ^= 0x8408
    return crc ^ 0xFFFF

def air_bits(nmea_bits):
    return [b for k in range(0, len(nmea_bits), 8) for b in nmea_bits[k:k + 8][::-1]]

def hdlc(nmea_bits, rnd):
    air = air_bits(nmea_bits)
    c = crc_bits(air)
    air += [(c >> i) & 1 for i in range(16)]
    st, ones = [], 0
    for b in air:
        st.append(b)
        if b:
            ones += 1
            if ones == 5: st.append(0); ones = 0
        else: ones = 0
    flag = [0, 1, 1, 1, 1, 1, 1, 0]
    data = [0, 1] * 12 + flag + st + flag + [rnd.getrandbits(1) for _ in range(24)]
    return data

def nrzi(bits):
    lvl, out = 1, []
    for b in bits:
        if b == 0: lvl = -lvl
        out.append(lvl)
    return np.array(out, float)

FS_SIM = 192000
SPS = FS_SIM // 9600

def gmsk_freq(symbols, bt=0.4, fd=2400.0):
    """Momentanfrequenz in Hz (Gauss-gefilterte NRZ-Folge)."""
    x = np.repeat(symbols, SPS)
    n = int(4 * SPS)
    t = (np.arange(-n, n + 1)) / SPS
    sig = math.sqrt(math.log(2)) / (2 * math.pi * bt)
    g = np.exp(-t * t / (2 * sig * sig)); g /= g.sum()
    return fd * np.convolve(x, g, 'same')

def make_signal(msgs, a, rnd):
    fs_out = a.fs
    nsl = int(a.dur / (60 / 2250.0))
    free = list(range(2, nsl - 8))
    placed = []
    for m in msgs:
        bits = hdlc(m['bits'], rnd)
        nslots = int(math.ceil((len(bits) + 8) / 256.0))
        for _ in range(200):
            s0 = rnd.choice(free)
            if all(s0 + k in free for k in range(nslots + 1)): break
        else: continue
        for k in range(nslots + 1): free.remove(s0 + k)
        placed.append((s0, bits, m))
    placed.sort(key=lambda p: p[0])
    N = int(a.dur * FS_SIM)
    f_inst = np.zeros(N)
    amp = np.zeros(N)
    truth = []
    for s0, bits, m in placed:
        sym = nrzi(bits)
        # leichte Taktabweichung des Senders: Bitdauer um ppm gestreckt
        fr = gmsk_freq(sym)
        if a.ppm:
            nn = int(len(fr) * (1 + a.ppm * 1e-6))
            fr = np.interp(np.linspace(0, len(fr) - 1, nn), np.arange(len(fr)), fr)
        start = int((s0 * (60 / 2250.0) + rnd.uniform(0.0002, 0.0012)) * FS_SIM)
        end = min(N, start + len(fr))
        f_inst[start:end] = fr[:end - start]
        ramp = int(0.0008 * FS_SIM)
        env = np.ones(end - start)
        env[:ramp] = np.linspace(0, 1, ramp); env[-ramp:] = np.linspace(1, 0, ramp)
        amp[start:end] = env * 10 ** (rnd.uniform(-a.fade, 0) / 20)
        truth.append((start / FS_SIM, m))
    phase = 2 * np.pi * np.cumsum(f_inst + a.dc) / FS_SIM
    sigc = amp * np.exp(1j * phase)
    # AWGN: SNR = Pcarrier / (N0 * 15 kHz)
    n0_var = (1.0 * FS_SIM / 15000.0) / (10 ** (a.snr / 10.0))        # Varianz je Abtastwert (komplex)
    nz = (np.random.RandomState(a.seed).randn(N) + 1j * np.random.RandomState(a.seed + 1).randn(N)) * math.sqrt(n0_var / 2)
    z = sigc + nz
    sos = signal.butter(4, 7500, 'low', fs=FS_SIM, output='sos')                  # ZF-Filter
    z = signal.sosfilt(sos, z.real) + 1j * signal.sosfilt(sos, z.imag)
    d = np.angle(z[1:] * np.conj(z[:-1])) * FS_SIM / (2 * np.pi)
    d = np.append(d, 0)
    sos = signal.butter(4, a.audio_bw, 'low', fs=FS_SIM, output='sos')
    d = signal.sosfilt(sos, d)
    if a.hp:
        d = signal.sosfilt(signal.butter(1, a.hp, 'high', fs=FS_SIM, output='sos'), d)
    from math import gcd
    g = gcd(FS_SIM, fs_out)
    y = signal.resample_poly(d, fs_out // g, FS_SIM // g)
    return y, truth

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--msgs', type=int, default=30)
    ap.add_argument('--types', default='all')
    ap.add_argument('--snr', type=float, default=25)
    ap.add_argument('--dc', type=float, default=0)
    ap.add_argument('--hp', type=float, default=0)
    ap.add_argument('--ppm', type=float, default=0)
    ap.add_argument('--fade', type=float, default=6, help='Pegelstreuung je Burst in dB')
    ap.add_argument('--audio-bw', type=float, default=6000)
    ap.add_argument('--fs', type=int, default=48000)
    ap.add_argument('--dur', type=float, default=0, help='Dauer s (Standard: aus Nachrichtenzahl)')
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    r = random.Random(a.seed)
    types = list(range(1, 28)) if a.types == 'all' else [int(x) for x in a.types.split(',')]
    msgs = []
    for i in range(a.msgs):
        t = types[i % len(types)] if a.types == 'all' else r.choice(types)
        bits = gen_msg(t, r)
        msgs.append(dict(type=t, bits=bits, nmea=to_nmea(bits, 'A', 0)))
    if not a.dur: a.dur = max(5.0, a.msgs * 0.09)
    y, truth = make_signal(msgs, a, r)
    import wave
    pcm = np.clip(y * (6000.0 / 2400.0), -32768, 32767).astype('<i2')      # +-2,4 kHz Hub = +-6000 LSB, keine Regelung
    w = wave.open(a.out, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(a.fs); w.writeframes(pcm.tobytes()); w.close()
    with open(a.out.rsplit('.', 1)[0] + '.truth.txt', 'w') as f:
        for t, m in sorted(truth, key=lambda x: x[0]):
            for l in m['nmea']: f.write(l + '\n')
    with open(a.out.rsplit('.', 1)[0] + '.times.txt', 'w') as f:
        for t, m in sorted(truth, key=lambda x: x[0]):
            f.write('%.5f %s\n' % (t, m['nmea'][0]))
    print(f'{a.out}: {len(truth)} Nachrichten, {a.dur:.1f} s, SNR {a.snr} dB', file=sys.stderr)

if __name__ == '__main__':
    main()
