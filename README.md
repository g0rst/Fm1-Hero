# FM1-Hero

Ein Rhythmusspiel im Stil von Guitar Hero für den **M-Vave FM-1**: Vier Spuren eines Beats, vier Drehregler.
Die Noten laufen von oben herunter und zeigen, in welche Richtung du den jeweiligen Regler drehen musst (↻ rechts, ↺ links) und wie lange.
Dabei veränderst du live den Sound und lernst nebenbei, was die Synth-Funktionen bewirken:

| Level | Die vier Regler | Lernziel |
| --- | --- | --- |
| 1 Helligkeit | Cutoff · Resonanz · Mod-Index · Ratio | Filter und FM-Obertöne |
| 2 Hüllkurve | Attack · Decay · Sustain · Release | ADSR (am FM-1: ENV-Taste, Regler 1–4) |
| 3 FM-Operatoren | Ratio · Mod-Level · Op-Stapel · Detune | wie Operatoren einander modulieren |
| 4 Effekte | Delay-Zeit · Feedback · Hall · Drive | Raum, Echo, Verzerrung |

## Stand

- **`prototype/index.html`**: spielbarer Prototyp im Browser (eine Datei, keine Installation).
  Eingabe per Maus/Touch (im Kreis um den Regler ziehen), Mausrad, Tasten Q/A, W/S, E/D, R/F oder MIDI-Controller (CC).
- **Firmware für den FM-1**: in Arbeit. Basis ist [Felucca](https://github.com/hugelton/Felucca) (siehe unten).
  Ziel ist ein eigener Spielmodus, der direkt auf dem FM-1 läuft: die vier Encoder als Eingabe, das Display als Notenspur.

## Herkunft und Lizenz

Dieses Repository enthält den Quellcode von **Felucca** (Leo Kuroshita, Hügelton Instruments), Stand
`ca1db43a78455ae747766a2738f8580380d40bc1`, lizenziert unter **GPL-3.0-only**. Die Originaldatei steht in `docs/FELUCCA-README.md`,
die Lizenzen in `LICENSE`, `LICENSING.md` und `LICENSES/`. Die Unterordner `firmware/bsp` und `firmware/engines` sind Git-Submodule
(`git submodule update --init`). Eigene Änderungen stehen ebenfalls unter GPL-3.0-only.

Felucca ist nicht mit M-Vave verbunden, und dieses Projekt ist nicht von M-Vave oder Hügelton Instruments unterstützt.
Die Installation von Custom-Firmware geschieht auf eigene Gefahr; der Felucca-Web-Installer bietet den Weg zurück zur offiziellen Firmware.
