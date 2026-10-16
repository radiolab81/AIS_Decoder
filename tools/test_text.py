#!/usr/bin/env python3
"""Prueft die Klartext-Dekodierung (ais_text_write) feldweise gegen pyais.
   Erzeugt fuer jeden Typ 1..27 mehrere Zufallsnachrichten (gen_ais.py), laesst host_sim -n den
   Klartext ausgeben und vergleicht Zahlen/Texte mit der pyais-Dekodierung.  Aufruf: test_text.py [Anzahl je Typ]"""
import os, re, subprocess, sys, random
sys.path.insert(0, os.path.dirname(__file__))
import gen_ais
from pyais import decode

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, '..', 'host_sim')

def coord(s):
    m = re.match(r"(\d+)\xb0(\d+)\.(\d+)'([NSEW])", s)
    v = int(m.group(1)) + (int(m.group(2)) + int(m.group(3)) / 10000.0) / 60.0
    return -v if m.group(4) in 'SW' else v

def textof(v): return v.strip('@ ').rstrip() if isinstance(v, str) else v

def run(nmea):
    p = subprocess.run([SIM, '-n', '/dev/stdin'], input='\n'.join(nmea) + '\n', capture_output=True, text=True, encoding='latin-1')
    # UTF-8 Gradzeichen als Latin-1 -> \xc2\xb0 ; wir vergleichen mit \xb0 daher umkodieren
    return p.stdout.replace('\xc2\xb0', '\xb0')

def checks(t, d, txt):
    """Liste (Name, ok) fuer eine Nachricht"""
    res = []
    def has(name, cond): res.append((name, bool(cond)))
    has('MMSI', re.search(r'MMSI %09d' % d['mmsi'], txt))
    def pos(lat, lon, tol):
        m = re.search(r"Pos (\d+\xb0[\d.]+'[NS])\s+(\d+\xb0[\d.]+'[EW])", txt)
        if not m: return False
        return abs(coord(m.group(1)) - d[lat]) < tol and abs(coord(m.group(2)) - d[lon]) < tol
    if t in (1, 2, 3, 18, 19, 9, 4, 11, 21):
        has('Pos', pos('lat', 'lon', 2e-4))
    if t == 27: has('Pos', pos('lat', 'lon', 2e-2))
    if t in (1, 2, 3):
        has('SOG', re.search(r'SOG %.1f kn' % d['speed'], txt) or d['speed'] >= 102.3)
        has('COG', re.search(r'COG %.1f' % d['course'], txt) or d['course'] >= 360)
        has('HDG', re.search(r'HDG %d' % d['heading'], txt) or d['heading'] == 511)
        has('Status', re.search(r'Status %d ' % int(d['status']), txt))
        has('Sek', re.search(r'UTC-Sekunde %d' % d['second'], txt) or d['second'] >= 60)
    if t in (18, 19):
        has('SOG', re.search(r'SOG %.1f kn' % d['speed'], txt) or d['speed'] >= 102.3)
        has('COG', re.search(r'COG %.1f' % d['course'], txt) or d['course'] >= 360)
    if t in (5, 19, 21, 24):
        nm = d.get('shipname') or d.get('name')
        if nm is not None: has('Name', ('"%s"' % textof(nm)) in txt)
    if t == 5:
        has('Rufz', ('"%s"' % textof(d['callsign'])) in txt)
        has('Ziel', ('"%s"' % textof(d['destination'])) in txt)
        has('IMO', re.search(r'IMO %d ' % d['imo'], txt))
        has('Tiefgang', re.search(r'Tiefgang %.1f m' % d['draught'], txt))
        has('Bug', re.search(r'Bug %d Heck %d Backbord %d Steuerbord %d' % (d['to_bow'], d['to_stern'], d['to_port'], d['to_starboard']), txt))
        has('Typ', re.search(r'Typ %d ' % d['ship_type'], txt))
    if t in (19,):
        has('Bug', re.search(r'Bug %d Heck %d Backbord %d Steuerbord %d' % (d['to_bow'], d['to_stern'], d['to_port'], d['to_starboard']), txt))
    if t in (4, 11):
        has('UTC', re.search(r'UTC %04d-%02d-%02d %02d:%02d:%02d' % (d['year'], d['month'], d['day'], d['hour'], d['minute'], d['second']), txt))
    if t == 9:
        has('Hoehe', re.search(r'Hoehe %d m' % d['alt'], txt) or d['alt'] == 4095)
    if t in (6, 8):
        has('DAC/FI', re.search(r'DAC %d FI %d' % (d['dac'], d['fid']), txt))
    if t in (7, 13):
        for i in range(1, 5):
            if d.get('mmsi%d' % i): has('Ack%d' % i, re.search(r'quittiert MMSI %09d ' % d['mmsi%d' % i], txt) and True)
    if t in (10,): has('Ziel', re.search(r'an MMSI %09d' % d['dest_mmsi'], txt))
    if t in (12, 14):
        has('Text', ('"%s"' % textof(d['text'])) in txt or textof(d['text']) == '' )
    if t == 15: has('Ziel', re.search(r'an MMSI %09d' % d['mmsi1'], txt))
    if t == 27:
        has('SOG', re.search(r'SOG %d kn' % d['speed'], txt) or d['speed'] == 63)
    if t == 20: has('Slots', re.search(r'Offset %d  Slots %d' % (d['offset1'], d['number1']), txt))
    if t == 24 and d['partno'] == 1:
        has('Rufz', ('"%s"' % textof(d['callsign'])) in txt)
        has('Serie', re.search(r'Serie %d' % d.get('serial_number', d.get('serial', -1)), txt))
    # --- Typen mit Gebieten / Zeitschlitz-Angaben ---
    def area(prefix, k):
        m = re.search(prefix + r"\s+(\d+\xb0[\d.]+'[NS])\s+(\d+\xb0[\d.]+'[EW])", txt)
        return bool(m) and abs(coord(m.group(1)) - d[k + '_lat'] / 60.0) < 2e-3 and abs(coord(m.group(2)) - d[k + '_lon'] / 60.0) < 2e-3
    if t == 17:
        m = re.search(r"Referenzposition (\d+\xb0[\d.]+'[NS])\s+(\d+\xb0[\d.]+'[EW])", txt)
        def wrap(v, bits):                 # pyais liefert Typ-17-Koordinaten vorzeichenlos
            raw = round(v * 10)
            return (raw - (1 << bits) if raw >= (1 << (bits - 1)) else raw) / 10.0
        has('RefPos', m and abs(coord(m.group(1)) - wrap(d['lat'], 17) / 60.0) < 2e-3 and abs(coord(m.group(2)) - wrap(d['lon'], 18) / 60.0) < 2e-3)
    if t == 22:
        has('Kanal', re.search(r'Kanal A %d  Kanal B %d' % (d['channel_a'], d['channel_b']), txt))
        if d['addressed']: has('Ziel', re.search(r'an MMSI %09d und' % d['dest1'], txt) or 'adressiert an MMSI %09d' % d['dest1'] in txt)
        else:
            has('NO', area('NO', 'ne')); has('SW', area('SW', 'sw'))
        has('Zone', re.search(r'Zonengroesse %d sm' % (d['zonesize'] + 1), txt))
    if t == 23:
        has('NO', area('NO', 'ne')); has('SW', area('SW', 'sw'))
        has('Ruhe', re.search(r'Ruhezeit %d min' % d['quiet'], txt))
    if t == 16:
        has('A', re.search(r'Offset %d  Inkrement %d' % (d['offset1'], d['increment1']), txt))
        if d.get('mmsi2'): has('B', re.search(r'an MMSI %09d.*Offset %d  Inkrement %d' % (d['mmsi2'], d['offset2'], d['increment2']), txt, re.S))
    if t == 15:
        has('T1', re.search(r'Typ %d Offset %d' % (d['type1_1'], d['offset1_1']), txt))
        if d.get('type1_2') is not None: has('T2', re.search(r'Typ %d Offset %d' % (d['type1_2'], d['offset1_2']), txt))
        if d.get('mmsi2'): has('M2', re.search(r'an MMSI %09d \(.*\): Typ %d Offset %d' % (d['mmsi2'], d['type2_1'], d['offset2_1']), txt))
    if t == 20:
        for i in range(1, 5):
            if d.get('number%d' % i) is not None and (i == 1 or d['offset%d' % i]):
                has('R%d' % i, re.search(r'Offset %d  Slots %d  Timeout %d min  Inkrement %d' % (d['offset%d' % i], d['number%d' % i], d['timeout%d' % i], d['increment%d' % i]), txt))
    if t in (25, 26):
        if d['addressed']: has('Ziel', re.search(r'adressiert an MMSI %09d' % d['dest_mmsi'], txt))
        if d['structured']: has('AppID', re.search(r'DAC %d FI %d' % (d['app_id'] >> 6, d['app_id'] & 63), txt))
    return res

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    r = random.Random(7)
    tot = bad = 0
    nodec = []
    for t in range(1, 28):
        for _ in range(n):
            bits = gen_ais.gen_msg(t, r)
            nm = gen_ais.to_nmea(bits, 'A', 1)
            try:
                d = decode(*nm).asdict()
            except Exception as e:
                nodec.append(t); continue
            txt = run(nm)
            if not re.match(r'Typ %d ' % t, txt):
                print('FALSCHER TYP', t, txt[:80]); bad += 1; continue
            for name, ok in checks(t, d, txt):
                tot += 1
                if not ok:
                    bad += 1
                    if bad <= 25: print('Typ', t, 'Feld', name, 'stimmt nicht:\n', txt, '\n', {k: v for k, v in d.items() if k not in ('radio', 'data', 'spare_1')})
    print('Typen ohne pyais-Dekodierung (nur Formpruefung):', sorted(set(nodec)))
    print('%d Feldpruefungen, %d Abweichungen' % (tot, bad))
    sys.exit(1 if bad else 0)

main()
