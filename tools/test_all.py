#!/usr/bin/env python3
"""Gesamttest: echte Aufnahmen (Regr. = Referenz aus frueheren, CRC- und pyais-geprueften Laeufen), synthetische Szenarien (OSR 4/5), Textdekoder.
   Ergebnis -> testsignal/testergebnis.txt.   Aufruf: python3 tools/test_all.py"""
import subprocess, os, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
os.chdir(root); tmp = tempfile.mkdtemp()
def run(c):
    r = subprocess.run(c, shell=True, capture_output=True, text=True)
    if r.returncode and ('gen_ais' in c or 'wav2raw' in c or 'compare' in c):
        sys.exit(f"FEHLER bei: {c}\n{r.stderr}")
    return r.stdout
def build(osr):
    run(f"gcc -O2 -std=gnu99 -DAIS_OSR={osr} -o {tmp}/hs{osr} host_sim.c ais_dsp.c ais_msg.c ais_app.c")
def decode(osr, raw, out):
    open(out, 'w').write('\n'.join(l for l in run(f"{tmp}/hs{osr} {raw}").splitlines() if l.startswith('!')) + '\n')
def cmp(truth, dec):
    return list(map(int, run(f"python3 tools/compare.py {truth} {dec}").split()[:4]))
rows = []; fail = 0
for name, wav, ref, extra in [("Skagen IQ (echt)", "ais_iq_48k_stereo16.wav", "ais_iq_referenz.txt", "--iq"),
                              ("Algarve NF (echt)", "ais_nf_48k_mono8.wav", "ais_nf_referenz.txt", ""),
                              ("L.A. Test1 8 Bit (Regr.)", "ais_test1_la_48k_mono8.wav", "ais_test1_referenz.txt", ""),
                              ("L.A. Test2 16 Bit (Regr.)", "ais_test2_la_48k_mono16.wav", "ais_test2_referenz.txt", "")]:
    for osr in (4, 5):
        build(osr); raw = f"{tmp}/r.raw"
        run(f"python3 tools/wav2raw.py testsignal/{wav} {raw} {extra} --osr {osr}")
        decode(osr, raw, f"{tmp}/d.txt"); t, ok, miss, ex = cmp(f"testsignal/{ref}", f"{tmp}/d.txt")
        rows.append((f"{name} OSR{osr}", t, ok, ex)); fail += ok < 0.9 * t
scen = [("sauber 25 dB", ""), ("SNR 14 dB", "--snr 14"), ("DC +800 Hz", "--dc 800"), ("HP 30 Hz", "--hp 30"),
        ("Takt +-50 ppm", "--ppm 50"), ("Schwund 12 dB", "--fade 12"), ("Audio 6 kHz BW", "--audio-bw 6000")]
for osr in (4, 5):
    build(osr)
    for n, o in scen:
        run(f"python3 tools/gen_ais.py {tmp}/s.wav --msgs 60 {o}")
        run(f"python3 tools/wav2raw.py {tmp}/s.wav {tmp}/s.raw --osr {osr}")
        decode(osr, f"{tmp}/s.raw", f"{tmp}/d.txt"); t, ok, miss, ex = cmp(f"{tmp}/s.truth.txt", f"{tmp}/d.txt")
        rows.append((f"synth {n} OSR{osr}", t, ok, ex))
        if n in ("sauber 25 dB",) and (ok < 0.95 * t or ex): fail += 1
r = subprocess.run("python3 tools/test_text.py", shell=True, capture_output=True, text=True)
lines = [l for l in (r.stdout + r.stderr).splitlines() if l.strip()]
txt = [lines[-1]] if lines else ["keine Ausgabe (pyais installiert? pip install pyais)"]
if r.returncode: txt = ["FEHLGESCHLAGEN: " + txt[0]]; fail += 1
out = ["Testergebnis AIS-Dekoder", "%-34s %7s %8s %9s" % ("Fall", "Soll", "Erkannt", "Ueberzaehl.")]
out += ["%-34s %7d %8d %9d" % r for r in rows] + ["Textdekoder: " + ' '.join(txt)]
open("testsignal/testergebnis.txt", "w").write('\n'.join(out) + '\n'); print('\n'.join(out))
sys.exit(1 if fail else 0)
