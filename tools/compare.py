#!/usr/bin/env python3
"""Vergleicht dekodierte !AIVDM-Saetze mit einer Wahrheitsliste (Sequenz-ID und Zeilenenden egal).
   compare.py truth.txt decoded.txt [-v]   -> Ausgabe: Wahrheit Dekodiert Fehlend Ueberzaehlig"""
import sys

def norm(l):
    p = l.strip().split(',')
    if len(p) < 7: return None
    p[3] = ''; p[4] = ''            # Sequenz-ID und Kanal
    s = ','.join(p)
    return s[:s.rindex('*')]        # Pruefsumme haengt von der Sequenz-ID ab

def load(fn, only_nmea=True):
    return [n for n in (norm(l) for l in open(fn) if l.startswith('!')) if n]

def check_cs(l):
    l = l.strip(); b = l[1:l.rindex('*')]; c = 0
    for ch in b: c ^= ord(ch)
    return l[l.rindex('*') + 1:] == '%02X' % c

if __name__ == '__main__':
    t = load(sys.argv[1]); o = load(sys.argv[2])
    bad_cs = [l for l in open(sys.argv[2]) if l.startswith('!') and not check_cs(l)]
    ts, os_ = set(t), set(o)
    miss, extra = ts - os_, os_ - ts
    print(f"{len(t)} {len(ts & os_)} {len(miss)} {len(extra)}" + (f" PRUEFSUMME-FEHLER {len(bad_cs)}" if bad_cs else ''))
    if '-v' in sys.argv:
        for x in sorted(miss): print(' fehlt', x)
        for x in sorted(extra): print(' extra', x)
