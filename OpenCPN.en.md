# AIS decoder + OpenCPN: ships live on a chart (Debian 13)

Use case: the ATmega328 decoder delivers `!AIVDM` sentences on its UART; **OpenCPN** shows the received ships on a chart. The sentences are taken over
**unchanged** – no conversion is needed. Tested with OpenCPN 5.10.2 on Debian 13 and the recordings from `testsignal/`
(played back into a real ATmega328 – harbour of Los Angeles/Long Beach).

![OpenCPN with AIS targets](doc/opencpn.png)

*OpenCPN 5.10.2: ships as triangles with course vectors, aids to navigation/shore stations as symbols, and the "AIS Target Query" window with MMSI, flag (MID 477 = Hong Kong), position, course, speed and turn rate.*

## Data path

```
scanner audio ──► ATmega328 (ADC) ──UART 38400 8N1──► /dev/ttyUSB0 ──► cat | nc ──TCP :10110──► OpenCPN
                    !AIVDM sentences                   (USB serial)    (or ser2net)            (AIS targets)
```

OpenCPN can also read the serial port **directly** (connection type *Serial*, `/dev/ttyUSB0`, 38400 baud). The TCP route is useful if
OpenCPN runs on another computer, several programs should listen in, or the port is not directly accessible.

## 1. Prerequisites

* The decoder outputs NMEA (default). Plain-text mode (`d`) should be **off** – it usually does not bother OpenCPN, but it occupies the line.
* Permission for the port: `sudo usermod -aG dialout $USER` (log in again afterwards).
* OpenCPN: `sudo apt install opencpn` (check the version with `apt policy opencpn`). A usable chart needs chart data (via the chart downloader or OpenCPN plug-ins); the AIS display itself works with the built-in world map as well.

## 2. Serial port to TCP

### Quick (one client, for trying out)
```
stty -F /dev/ttyUSB0 38400 cs8 -cstopb -parenb raw clocal -echo -hupcl
cat /dev/ttyUSB0 | nc -l 10110
```
* `-hupcl` prevents closing/opening the port from resetting Arduino boards via DTR.
* `nc -l 10110` waits for **one** client and ends when it disconnects – then restart both lines (or wrap them in `while true; do …; done`).
* Check that data flows: in a second terminal run `nc 127.0.0.1 10110` – `!AIVDM,…` lines must appear.

Equivalent with `socat`:
```
socat /dev/ttyUSB0,b38400,raw,echo=0 TCP-LISTEN:10110,reuseaddr
```

### Permanent and for several clients: ser2net
```
sudo apt install ser2net
```
`/etc/ser2net.yaml`:
```
connection: &ais
  accepter: tcp,10110
  enable: on
  connector: serialdev,/dev/ttyUSB0,38400n81,local
```
```
sudo systemctl restart ser2net
sudo systemctl enable ser2net
```
Notes: a fixed device name (`/dev/serial/by-id/usb-…`) instead of `/dev/ttyUSB0` avoids problems when several USB serial adapters are attached.
The connection is **unencrypted and without authentication** – offer it only on the local network, restrict it with a firewall if necessary.

## 3. Setting up OpenCPN

1. **Options** (gear icon) → **Connections** → **Add Connection**.
2. Choose **Network**, protocol **TCP**, address `127.0.0.1` (or the IP of the computer with the decoder), port `10110`.
3. Tick **Receive input on this port**; the checksum check can stay on (the sentences carry the correct checksum).
4. Apply. The connection appears in the list; its status indicator turns to "connected".
5. Enable menu **AIS → Show AIS targets**, and if needed switch on the display options (course vector, names, target list) in the AIS settings.
6. Click a ship → **AIS Target Query** (as in the picture) with MMSI, flag, position, speed, course, heading, turn rate; type 5 data (name, call sign, destination, dimensions) follows once the ship sends that message (every 6 minutes).

*(The labels may differ with language setting and version; the sequence is as described for 5.10.)*

## 4. If nothing shows up

| Symptom | Cause / remedy |
|---|---|
| No lines from `cat /dev/ttyUSB0` | Baud rate (38400), cable/adapter, is the decoder running? Send command `s` (statistics) via `make monitor`. |
| `Permission denied` | User is not in the `dialout` group. |
| `nc` says "Address already in use" | An old `nc`/`ser2net` is still listening on port 10110: `ss -ltnp | grep 10110`. |
| Connected, but no ships | AIS display switched off in OpenCPN; checksum errors (lines with `#` or plain text in between: switch `d` off); position outside the visible chart area. |
| Ships appear, vanish after minutes | Normal: OpenCPN hides targets after the configured time without a new report. |
| Chart empty / only land outline | Chart data missing – AIS targets are drawn anyway. |
| Own position wrong in the status bar | That is OpenCPN's "Ship" position, not from AIS; without a GPS source the last value stays. |

## 5. Recording and replay

To record the sentences: `nc 127.0.0.1 10110 | tee ais.nmea` (only for checking the data). Such a file can be shown as plain text with
`./host_sim -n ais.nmea` (test of the text decoder) or replayed into OpenCPN via a second network connection or a data-recording plug-in.

---
*This document belongs to the AIS decoder project, see [README.md](README.md) / [README.de.md](README.de.md).*
