# CrossPoint + LifePoints

Dieser Fork ist das offizielle [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 2.0.0
mit **einem zusätzlichen Menüpunkt: LifePoints**, einem Lebenspunkte-Zähler für zwei Spieler
(gedacht für **Flesh and Blood**, funktioniert aber für jedes Kartenspiel mit Lebenspunkten).

Alles andere von CrossPoint (Bücher lesen, Bibliothek, Dateiübertragung, Einstellungen, Themes)
bleibt **unverändert** erhalten.

Getestet auf: **Xteink X4** (ESP32-C3, 800×480 E-Ink, 16 MB Flash).

---

## Was LifePoints kann

- Zwei Spieler, beide starten mit **40** Lebenspunkten.
- Gerät liegt im **Hochformat** zwischen den Spielern, die 4 Fronttasten unten.
- **Obere Hälfte** = oberer Spieler, **untere Hälfte** = unterer Spieler, getrennt durch eine Linie.
- Riesige schwarze Zahlen, **seitlich gedreht**, damit jeder Spieler seine Zahl lesen kann
  (oben 90° gegen den Uhrzeigersinn, unten 90° im Uhrzeigersinn).
- Unten kleine Kreise mit **− + + −** direkt über den Tasten, sonst keine Texte.
- Der Spielstand bleibt erhalten, wenn man kurz ins Hauptmenü geht
  (aber nicht, wenn das Gerät schlafen geht oder neu startet).

Den Menüpunkt findest du im Hauptmenü ganz unten („LifePoints“).
Im **Cover-Grid-Theme** ist es das **Herz-Symbol** in der unteren Leiste.

## Bedienung

Tasten 1–4 = die vier Fronttasten **von links nach rechts**.

| Taste | Kreis | Wirkung |
|---|---|---|
| 1 | − | unterer Spieler −1 |
| 2 | + | unterer Spieler +1 |
| 3 | + | oberer Spieler +1 |
| 4 | − | oberer Spieler −1 |

- **Kurz drücken** = 1 Punkt.
- **Halten (ab 0,6 Sekunden)** = 5 Punkte, zählt einmal beim Loslassen.
- **Reset auf 40/40:** eine **Seitentaste 1 Sekunde gedrückt halten**.
- **Zurück ins Hauptmenü:** Tasten **1, 4, 1, 4** kurz hintereinander (innerhalb von 3 Sekunden).
  Die Punkte, die sich dabei geändert haben, werden automatisch zurückgesetzt.

**Bildschirm:** Nach jeder Eingabe wird nur der Inhalt schnell neu gezeichnet.
Nach 10 Änderungen und beim Reset blinkt der Bildschirm einmal kräftig (voller Refresh),
damit keine Schatten zurückbleiben. Das ist gewollt.

### Warum Reset über die Seitentaste?

Ursprünglich war „Taste 2 und 3 gleichzeitig“ geplant. Beim X4 hängen aber alle vier Fronttasten
an **einem einzigen Messeingang** (Widerstandsleiter am ADC). Zwei gleichzeitig gedrückte Tasten
ergeben einen Mischwert, der als eine einzelne, falsche Taste erkannt wird. Die Seitentasten hängen
an einem eigenen Eingang und werden deshalb sicher erkannt.

---

## Installieren

Die fertige Firmware gibt es unter **[Releases](../../releases)** als `custom.bin`.
Das ist ein App-Image für Adresse **0x10000** (kein komplettes Flash-Abbild).

### Weg 1: über die SD-Karte (am einfachsten)

1. `custom.bin` auf die SD-Karte des X4 kopieren.
2. Am Gerät in CrossPoint: **Einstellungen → System → SD-Karte Firmware Update**
   (englisch: *Settings → System → SD Card Firmware Update*) und die Datei auswählen.

CrossPoint kümmert sich dabei selbst darum, in welchen Programmbereich geschrieben wird.

### Weg 2: per USB mit esptool (Mac/PC)

Vorher **unbedingt ein Backup** machen (siehe unten). Dann:

```bash
esptool --chip esp32c3 write-flash 0x10000 custom.bin
```

Hinweis: Das funktioniert direkt nur, wenn auf dem Gerät gerade der Bereich `app0` aktiv ist.
Nach früheren Online-Updates kann `app1` aktiv sein; dann ist Weg 1 einfacher.

## Backup und Wiederherstellung

**Backup** des kompletten 16-MB-Speichers (liest nur, ändert nichts):

```bash
esptool --chip esp32c3 --no-stub read-flash 0 0x1000000 backup_x4_16MB.bin
```

`--no-stub` ist langsamer, war beim X4 aber nötig: Mit dem schnellen Hilfs-Lader brach das Lesen
bei Adresse 0x140000 jedes Mal ab.

**Wiederherstellen** (schreibt das Gerät exakt in den alten Zustand zurück):

```bash
esptool --chip esp32c3 write-flash 0 backup_x4_16MB.bin
```

Reagiert das Gerät nicht: Power-Taste gedrückt halten, kurz Reset drücken, Power loslassen
(Download-Modus), dann den Befehl erneut ausführen.

Das Backup gehört **nicht** ins Internet: Es kann WLAN-Passwörter und Anmeldedaten enthalten.

---

## Selbst bauen

Gebaut wird mit PlatformIO, Umgebung `gh_release`:

```bash
pio run -e gh_release
```

Ergebnis: `.pio/build/gh_release/firmware.bin` (das ist die `custom.bin`).

Zwei Stolperfallen auf dem Mac:

- Der Projektordner darf **keine Leerzeichen** im Pfad haben (ESP-IDF bricht sonst ab).
- PlatformIO Core **6.2.0** bricht mit `No module named 'SCons.Tool.FortranCommon'` ab.
  Lösung (wie im offiziellen CI): Core 6.1.19 in PlatformIOs eigener Python-Umgebung installieren:
  ```bash
  ~/.platformio/penv/bin/python -m pip install "pioarduino==6.1.19"
  ```
  Danach einmal `.pio/libdeps/gh_release` löschen und neu bauen.

## Was im Code geändert wurde

| Datei | Änderung |
|---|---|
| `src/activities/lifepoints/LifePointsActivity.*` | **neu**: der Zähler (Tasten, Anzeige, Refresh) |
| `src/components/icons/heart.h` | **neu**: Herz-Symbol für das Cover-Grid-Theme |
| `src/activities/ActivityManager.h` | Menüpunkt `LIFEPOINTS` ergänzt |
| `src/activities/home/HomeActivity.*` | Menüpunkt im Hauptmenü |
| `src/components/CoverGridHomeUi.*` | sechstes Symbol (Herz) im Cover-Grid |

Zweige in diesem Repo:

- `lifepoints` – diese Version (Standard)
- `base-2.0.0` – das unveränderte offizielle CrossPoint 2.0.0
- `develop` – Stand des offiziellen Repos zum Zeitpunkt des Forks
