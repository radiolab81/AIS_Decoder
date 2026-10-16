#!/usr/bin/env python3
"""Erzeugt die Grafiken fuer die READMEs (doc/*.svg, doc/burst.png).  Aufruf: python3 tools/make_doc.py
   Die SVGs sind reiner Text (keine Bibliothek noetig); burst.png braucht numpy/scipy/matplotlib."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE); OUT = os.path.join(ROOT, 'doc')
os.makedirs(OUT, exist_ok=True)
FONT = "font-family='Helvetica,Arial,sans-serif'"
C = dict(bg='#ffffff', ink='#1f2937', mute='#6b7280', line='#9ca3af', a='#dbeafe', b='#dcfce7', c='#fef3c7',
         d='#fce7f3', e='#e5e7eb', acc='#2563eb', red='#dc2626')

def svg(w, h, body):
    return (f"<svg xmlns='http://www.w3.org/2000/svg' width='{w}' height='{h}' viewBox='0 0 {w} {h}' {FONT}>"
            f"<rect width='{w}' height='{h}' fill='{C['bg']}'/>{body}</svg>\n")

def esc(s): return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

def box(x, y, w, h, text, fill, size=12, sub=None, stroke=None):
    s = f"<rect x='{x}' y='{y}' width='{w}' height='{h}' rx='5' fill='{fill}' stroke='{stroke or C['line']}'/>"
    ty = y + h / 2 + (0 if sub else 4)
    s += f"<text x='{x + w / 2}' y='{ty - (6 if sub else 0)}' font-size='{size}' text-anchor='middle' fill='{C['ink']}'>{esc(text)}</text>"
    if sub: s += f"<text x='{x + w / 2}' y='{ty + 10}' font-size='{size - 2}' text-anchor='middle' fill='{C['mute']}'>{esc(sub)}</text>"
    return s

def arrow(x1, y1, x2, y2):
    return (f"<line x1='{x1}' y1='{y1}' x2='{x2}' y2='{y2}' stroke='{C['mute']}' stroke-width='1.5'/>"
            f"<polygon points='{x2},{y2} {x2 - (7 if x2 > x1 else -7 if x2 < x1 else 0)},{y2 - (4 if x2 != x1 else (7 if y2 > y1 else -7)) + (0 if x2 != x1 else 0)} "
            f"{x2 - (7 if x2 > x1 else -7 if x2 < x1 else 0)},{y2 + (4 if x2 != x1 else (-7 if y2 > y1 else 7))}' fill='{C['mute']}'/>")

# ---------------------------------------------------------------- 1. Signalkette
def chain():
    b = f"<text x='20' y='28' font-size='16' font-weight='bold' fill='{C['ink']}'>Signalkette: vom Funksignal zum NMEA-Satz</text>"
    row1 = [("Antenne", "161,975 / 162,025 MHz", C['e']), ("NFM-Scanner", "FM-Diskriminator", C['a']),
            ("ADC 10 Bit", "38,4 kHz = 4 x 9600", C['a']), ("Integrate & Dump", "Summe ueber 1 Bit", C['b'])]
    row2 = [("Schwelle", "Hi/Lo-Tracker je Phase", C['b']), ("NRZI + Flag 0x7E", "Entstopfen, 4 Bitphasen", C['b']),
            ("Rahmenpuffer", "Warteschlange", C['c']), ("CRC-16 + Typ/Laenge", "Hauptschleife", C['c']),
            ("!AIVDM / Klartext", "UART 38400", C['d'])]
    x = 20; y = 50
    for i, (t, s, f) in enumerate(row1):
        b += box(x, y, 190, 52, t, f, 13, s)
        if i < len(row1) - 1: b += arrow(x + 190, y + 26, x + 215, y + 26)
        x += 215
    b += f"<path d='M{20 + 3 * 215 + 95},{y + 52} V{y + 80} H{20 + 95} V{y + 100}' fill='none' stroke='{C['mute']}' stroke-width='1.5'/>"
    b += f"<polygon points='{20 + 95},{y + 108} {20 + 90},{y + 100} {20 + 100},{y + 100}' fill='{C['mute']}'/>"
    x = 20; y2 = y + 108
    for i, (t, s, f) in enumerate(row2):
        b += box(x, y2, 160, 52, t, f, 12, s)
        if i < len(row2) - 1: b += arrow(x + 160, y2 + 26, x + 172, y2 + 26)
        x += 172
    b += f"<text x='20' y='{y2 + 86}' font-size='12' fill='{C['mute']}'>Blau: Analog/Abtastung - Gruen: Bitdemodulation (ais_dsp.c, in der Interruptroutine) - Gelb: Rahmenpruefung (ais_msg.c) - Rosa: Ausgabe (ais_app.c)</text>"
    b += f"<text x='20' y='{y2 + 106}' font-size='12' fill='{C['mute']}'>Kein I/Q, keine Traegernachfuehrung: der Scanner liefert bereits die demodulierte Datenfolge.</text>"
    return svg(900, y2 + 125, b)

# ---------------------------------------------------------------- 2. HDLC-Rahmen, NRZI, Bitstopfen
def frame():
    b = f"<text x='20' y='28' font-size='16' font-weight='bold' fill='{C['ink']}'>AIS-Burst: HDLC-Rahmen, Bitstopfen, NRZI</text>"
    parts = [("Training", "24 Bit 0101...", 120, C['e']), ("Flag", "0x7E", 80, C['a']), ("Nutzdaten (Nachricht)", "168 ... 1008 Bit", 330, C['b']),
             ("FCS", "CRC-16", 80, C['c']), ("Flag", "0x7E", 80, C['a']), ("Puffer", "Jitter", 100, C['e'])]
    x = 20
    for t, s, w, f in parts:
        b += box(x, 48, w, 54, t, f, 12, s); x += w
    b += f"<text x='20' y='122' font-size='12' fill='{C['mute']}'>Ein Slot dauert 26,67 ms = 256 Bit bei 9600 Bit/s.  Ein Burst belegt 1 Slot (meist 168 Bit) oder bis zu 5 Slots.</text>"
    # Bitstopfen
    b += f"<text x='20' y='160' font-size='14' font-weight='bold' fill='{C['ink']}'>Bitstopfen: nach fuenf Einsen fuegt der Sender eine 0 ein</text>"
    seq = "0 1 1 1 1 1 [0] 1 1 0 1 1 1 1 1 [0] 0 1"
    x = 20; y = 176
    for tok in seq.split():
        ins = tok.startswith('[')
        v = tok.strip('[]')
        b += box(x, y, 30, 30, v, C['d'] if ins else C['bg'], 14, None, C['red'] if ins else C['line']); x += 34
    b += f"<text x='20' y='{y + 52}' font-size='12' fill='{C['mute']}'>Rot: eingefuegte Stopfbits - der Empfaenger entfernt sie wieder.  Sechs Einsen in Folge gibt es nur im Flag 01111110, sieben nie (= Abbruch).</text>"
    # NRZI
    y0 = 290
    b += f"<text x='20' y='{y0 - 20}' font-size='14' font-weight='bold' fill='{C['ink']}'>NRZI: Bit 0 = Pegelwechsel, Bit 1 = Pegel bleibt</text>"
    bits = [0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1]
    w = 60; hi = y0 + 20; lo = y0 + 80; lvl = 1; pts = []
    for i, bt in enumerate(bits):
        if bt == 0: lvl ^= 1
        yy = hi if lvl else lo
        if i == 0: pts.append((20, yy))
        pts.append((20 + i * w, yy)); pts.append((20 + (i + 1) * w, yy))
        b += f"<text x='{20 + i * w + w / 2}' y='{y0 + 4}' font-size='15' text-anchor='middle' fill='{C['ink']}'>{bt}</text>"
        b += f"<line x1='{20 + i * w}' y1='{y0 + 12}' x2='{20 + i * w}' y2='{lo + 14}' stroke='{C['e']}'/>"
    b += f"<line x1='{20 + len(bits) * w}' y1='{y0 + 12}' x2='{20 + len(bits) * w}' y2='{lo + 14}' stroke='{C['e']}'/>"
    d = ' '.join(f"{'M' if i == 0 else 'L'}{px},{py}" for i, (px, py) in enumerate(pts))
    b += f"<path d='{d}' fill='none' stroke='{C['acc']}' stroke-width='2.5'/>"
    # GMSK-verrundet (gestrichelt)
    sm = []
    for i, (px, py) in enumerate(pts): sm.append((px, py))
    b += f"<text x='{20 + len(bits) * w + 10}' y='{hi + 4}' font-size='12' fill='{C['mute']}'>+2,4 kHz</text>"
    b += f"<text x='{20 + len(bits) * w + 10}' y='{lo + 4}' font-size='12' fill='{C['mute']}'>-2,4 kHz</text>"
    b += f"<text x='20' y='{lo + 40}' font-size='12' fill='{C['mute']}'>Blau: ideal; real rundet das Gauss-Filter (BT = 0,4) die Flanken ab (siehe burst.png).  Nur der Wechsel zaehlt:</text>"
    b += f"<text x='20' y='{lo + 58}' font-size='12' fill='{C['mute']}'>Polaritaet und Gleichanteil (Frequenzablage der Quarze) sind fuer die Dekodierung bedeutungslos.</text>"
    return svg(900, lo + 75, b)

# ---------------------------------------------------------------- 3. Nachrichten-Layouts
def layout(title, sub, fields, rowbits=None):
    pal = [C['a'], C['b'], C['c'], C['d'], C['e']]
    maxw = 860; x = 20; y = 62; rowh = 52; b = f"<text x='20' y='28' font-size='16' font-weight='bold' fill='{C['ink']}'>{esc(title)}</text>"
    b += f"<text x='20' y='46' font-size='12' fill='{C['mute']}'>{esc(sub)}</text>"
    pos = 0; k = 0
    for name, n in fields:
        w = max(72, min(300, 14 + n * 2.2)) if n < 140 else 300
        if x + w > 20 + maxw: x = 20; y += rowh + 20
        fill = pal[k % len(pal)]; k += 1
        b += box(x, y, w, rowh, name, fill, 11, f"{n} Bit  ({pos})")
        x += w + 3; pos += n
    return svg(900, y + rowh + 24, b + f"<text x='20' y='{y + rowh + 18}' font-size='11' fill='{C['mute']}'>Klammer = Bitposition im Nachrichtenfeld (MSB zuerst, Bit 0 = erstes Bit des Typs), zusammen {pos} Bit.</text>")

T1 = [("Typ", 6), ("Repeat", 2), ("MMSI", 30), ("Status", 4), ("Drehrate", 8), ("SOG", 10), ("Genau.", 1), ("Laenge", 28), ("Breite", 27),
      ("COG", 12), ("HDG", 9), ("UTC-s", 6), ("Man.", 2), ("-", 3), ("RAIM", 1), ("Funk", 19)]
T5 = [("Typ", 6), ("Repeat", 2), ("MMSI", 30), ("AIS-Vers.", 2), ("IMO", 30), ("Rufzeichen", 42), ("Name", 120), ("Schiffstyp", 8),
      ("Abmessungen", 30), ("EPFD", 4), ("ETA", 20), ("Tiefgang", 8), ("Ziel", 120), ("DTE", 1), ("-", 1)]
T18 = [("Typ", 6), ("Repeat", 2), ("MMSI", 30), ("-", 8), ("SOG", 10), ("Genau.", 1), ("Laenge", 28), ("Breite", 27), ("COG", 12), ("HDG", 9),
       ("UTC-s", 6), ("-", 2), ("CS", 1), ("Disp.", 1), ("DSC", 1), ("Band", 1), ("Msg22", 1), ("Zugew.", 1), ("RAIM", 1), ("Funk", 20)]
T8 = [("Typ", 6), ("Repeat", 2), ("MMSI", 30), ("-", 2), ("DAC", 10), ("FI", 6), ("Anwendungsdaten", 120)]

def nmea():
    ex = "!AIVDM,1,1,,A,15MqDMPP00GRo;RCDf`@0?vT0@;W,0*74"
    b = f"<text x='20' y='28' font-size='16' font-weight='bold' fill='{C['ink']}'>Von den Bits zum NMEA-Satz</text>"
    b += f"<text x='20' y='70' font-size='15' font-family='monospace' fill='{C['ink']}'>{esc(ex)}</text>"
    items = [("!AIVDM", 20, "Sprecher AI, Format VDM"), ("1,1", 92, "Teil 1 von 1"), ("(leer)", 120, "Sequenz-ID (nur Mehrteiler)"),
             ("A", 146, "Kanal A / B"), ("15Mq...;W", 160, "Nutzdaten: 6-Bit-ASCII-Armor"), ("0", 392, "Fuellbits am Ende"), ("*74", 408, "XOR-Pruefsumme")]
    rows = [("!AIVDM", "Sprecher AI, Satzformat VDM (Daten von anderen Schiffen)"), ("1,1", "Satz 1 von 1 (lange Nachrichten: 2 oder 3 Saetze, je max. 60 Zeichen)"),
            ("(leer)", "Sequenz-ID 0..9, nur bei Mehrteilern gesetzt"), ("A", "Funkkanal: A = 161,975 MHz, B = 162,025 MHz"),
            ("15Mq...0@;W", "Nutzdaten: je 6 Bit -> 1 ASCII-Zeichen (Wert + 48, ab 40 weitere +8)"), ("0", "Fuellbits, damit die Bitzahl durch 6 teilbar wird"),
            ("*74", "Pruefsumme: XOR aller Zeichen zwischen ! und *")]
    y = 100
    for k, d in rows:
        b += f"<text x='40' y='{y}' font-size='13' font-family='monospace' fill='{C['acc']}'>{esc(k)}</text><text x='200' y='{y}' font-size='13' fill='{C['ink']}'>{esc(d)}</text>"; y += 22
    b += f"<text x='20' y='{y + 12}' font-size='13' fill='{C['ink']}'>Zeichen -> Bits:  '1' = 49 - 48 = 1 = 000001 (Typ 1),  '5' = 53 - 48 = 5 = 000101 (Repeat 0 + 3 Bit MMSI)  usw.</text>"
    return svg(900, y + 30, b)

def types():
    rows = [("1, 2, 3", "Positionsmeldung Klasse A", "168", "alle 2-10 s (fahrend) / 3 min (vor Anker)"),
            ("4", "Basisstation: UTC, Position", "168", "alle 10 s"),
            ("5", "Statische und Reisedaten Klasse A", "424", "alle 6 min, 2 Slots"),
            ("6 / 7", "Adressierte Binaernachricht / Quittung", "72-1008", "bei Bedarf"),
            ("8", "Binaer-Broadcast (DAC/FI, Wetter, Hydrografie ...)", "56-1008", "bei Bedarf"),
            ("9", "SAR-Luftfahrzeug", "168", "alle 10 s"),
            ("10 / 11", "UTC-Abfrage / -Antwort", "72 / 168", "bei Bedarf"),
            ("12 / 13", "Adressierter Sicherheitstext / Quittung", "72-1008", "bei Bedarf"),
            ("14", "Sicherheitstext (Broadcast)", "40-1008", "bei Bedarf"),
            ("15 / 16 / 20 / 22 / 23", "Abfrage, Zuweisung, Slot-Reservierung, Kanal-/Gruppenverwaltung", "40-168", "Basisstationen"),
            ("17", "DGNSS-Korrekturdaten (RTCM)", "80-816", "Basisstation"),
            ("18 / 19", "Positionsmeldung Klasse B (CS) / erweitert", "168 / 312", "alle 30 s"),
            ("21", "Seezeichen (AtoN): Name, Position, Typ", "272-360", "alle 3 min"),
            ("24", "Klasse-B-Statik, Teil A: Name / Teil B: Typ, Abm.", "160 / 168", "alle 6 min"),
            ("25 / 26", "Einzelslot- / Mehrslot-Binaernachricht", "bis 1008", "bei Bedarf"),
            ("27", "Langstreckenposition (Satellit-AIS)", "96", "alle 3 min")]
    b = f"<text x='20' y='28' font-size='16' font-weight='bold' fill='{C['ink']}'>Die AIS-Nachrichtentypen (ITU-R M.1371-5) - alle 27 werden dekodiert</text>"
    b += f"<text x='20' y='56' font-size='12' font-weight='bold' fill='{C['ink']}'>Typ</text><text x='190' y='56' font-size='12' font-weight='bold' fill='{C['ink']}'>Inhalt</text>"
    b += f"<text x='560' y='56' font-size='12' font-weight='bold' fill='{C['ink']}'>Laenge (Bit)</text><text x='650' y='56' font-size='12' font-weight='bold' fill='{C['ink']}'>Sendefolge</text>"
    y = 78
    for i, (t, d, n, r) in enumerate(rows):
        if i % 2 == 0: b += f"<rect x='12' y='{y - 15}' width='876' height='22' fill='#f3f4f6'/>"
        b += f"<text x='20' y='{y}' font-size='12' fill='{C['acc']}'>{esc(t)}</text><text x='190' y='{y}' font-size='12' fill='{C['ink']}'>{esc(d)}</text>"
        b += f"<text x='560' y='{y}' font-size='12' fill='{C['ink']}'>{n}</text><text x='650' y='{y}' font-size='12' fill='{C['mute']}'>{esc(r)}</text>"
        y += 22
    return svg(900, y + 10, b)

def burst_png():
    """Oben: sauberer simulierter Burst mit Feldern (gen_ais.py); unten: echter Burst (Skagen, SDR-IQ, per Diskriminator)."""
    try:
        import numpy as np, subprocess, tempfile, wave
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        from scipy import signal
        sys.path.insert(0, HERE); import ref_decoder as rd
    except Exception as e:
        print('burst.png uebersprungen:', e); return
    ms = 1000.0 / 9600; nb = 24 + 8 + 168 + 16 + 8
    tmp = tempfile.mkdtemp(); wv = os.path.join(tmp, 'b.wav')
    subprocess.run([sys.executable, os.path.join(HERE, 'gen_ais.py'), wv, '--msgs', '1', '--types', '1', '--snr', '40', '--fade', '0',
                    '--audio-bw', '7000', '--dur', '1'], capture_output=True)
    ts = float(open(os.path.join(tmp, 'b.times.txt')).read().split()[0])
    w = wave.open(wv); fs1 = w.getframerate(); y = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float) / 6000.0
    n0 = int((ts - 2 * ms / 1000) * fs1); seg1 = y[n0:n0 + int((nb + 4) * ms / 1000 * fs1)]; t1 = np.arange(len(seg1)) / fs1 * 1000 - 2 * ms
    x, fs = rd.read_wav(os.path.join(ROOT, 'testsignal', 'ais_iq_48k_stereo16.wav'))
    res = sorted(rd.decode(x, fs, True, 'A'), key=lambda r: r[0]); t_flag = [r for r in res if len(r[1]) == 168][0][0]
    z = x[:, 0] + 1j * x[:, 1]
    sos = signal.butter(6, 7500, 'low', fs=fs, output='sos'); z = signal.sosfilt(sos, z.real) + 1j * signal.sosfilt(sos, z.imag)
    d = np.append(np.angle(z[1:] * np.conj(z[:-1])), 0); d = signal.sosfilt(signal.butter(4, 5500, 'low', fs=fs, output='sos'), d)
    n2 = int((t_flag - 26 * ms / 1000) * fs); seg2 = d[n2:n2 + int((nb + 4) * ms / 1000 * fs)]; seg2 = (seg2 - np.median(seg2)); seg2 /= abs(seg2).max() * 0.5
    t2 = np.arange(len(seg2)) / fs * 1000 - 2 * ms
    fig, ax = plt.subplots(2, 1, figsize=(12, 5.8), sharex=True)
    ax[0].plot(t1, seg1, color='#2563eb', lw=1.3)
    for k in range(nb + 1): ax[0].axvline(k * ms, color='#e5e7eb', lw=.4)
    for a_, b_, c_ in [(0, 24, '#e5e7eb'), (24, 32, '#bfdbfe'), (32, 200, '#dcfce7'), (200, 216, '#fde68a'), (216, 224, '#bfdbfe')]:
        ax[0].axvspan(a_ * ms, b_ * ms, color=c_, alpha=.7)
    top = 1.45
    for xx, tt in [(12, 'Training'), (28, 'Flag'), (116, 'Nutzdaten (168 Bit: Typ, MMSI, Position ...)'), (208, 'FCS'), (220, 'Flag')]:
        ax[0].text(xx * ms, top, tt, ha='center', fontsize=9)
    ax[0].set_ylim(-1.5, 1.7); ax[0].set_ylabel('simuliert, sauber')
    ax[0].set_title('AIS-Burst am Ausgang eines FM-Diskriminators: 24 Bit Training, Flag, 168 Datenbit, FCS, Flag = 224 Bit = 23,3 ms (Slot: 256 Bit)')
    ax[1].plot(t2, seg2, color='#2563eb', lw=1.0)
    for k in range(nb + 1): ax[1].axvline(k * ms, color='#e5e7eb', lw=.4)
    ax[1].set_ylabel('real aufgenommen'); ax[1].set_xlabel('Zeit [ms]   (Raster: eine Bitzeit = 104 us)'); ax[1].set_ylim(-2.2, 2.2)
    ax[1].text(0.2, 1.8, 'Skagen (SDR-IQ, hier per Diskriminator demoduliert): dieselbe Struktur, aber mit Rauschen und Schwund', fontsize=9)
    plt.tight_layout(); plt.savefig(os.path.join(OUT, 'burst.png'), dpi=100)

if __name__ == '__main__':
    for fn, f in [('signalkette.svg', chain()), ('rahmen.svg', frame()), ('nmea.svg', nmea()), ('nachrichtentypen.svg', types()),
                  ('typ1.svg', layout('Typ 1/2/3 - Positionsmeldung Klasse A (168 Bit)', 'Beispiel: Schiff unterwegs, Position, Kurs, Geschwindigkeit', T1)),
                  ('typ5.svg', layout('Typ 5 - Statische und Reisedaten Klasse A (424 Bit, 2 Slots)', 'Name, Rufzeichen, Abmessungen, Ziel, ETA', T5)),
                  ('typ18.svg', layout('Typ 18 - Positionsmeldung Klasse B (168 Bit)', 'Sportboote und Kleinfahrzeuge', T18)),
                  ('typ8.svg', layout('Typ 8 - Binaer-Broadcast', 'DAC = Landeskennung der Anwendung, FI = Funktionskennung, danach Anwendungsdaten (bis 952 Bit)', T8))]:
        open(os.path.join(OUT, fn), 'w').write(f)
    burst_png()
    print('doc/ fertig')
