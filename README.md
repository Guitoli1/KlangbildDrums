# Klangbild Drums

Drum-Kanalzug im Klangbild-Vintage-Look – AU, VST3 und Standalone für macOS (Logic Pro).

## Signalweg

```
BAND → EQ → BUS → PLATTE → Ausgang
```

| Modul | Aufgabe |
|---|---|
| **Band** | Bandmaschine – Sättigung, weiche Transienten, „Kleber“ |
| **EQ** | 8-Band-EQ mit Analyzer – Mulm raus, Kick-Tiefe, Snare-Knack, Becken-Luft |
| **Bus** | VCA-Buskompressor mit Mix-Regler für Parallel-Kompression |
| **Platte** | Plattenhall für Snare und Raum |

Ein Klick auf eine Kachel öffnet das Modul, der Schalter darunter schaltet es an/aus.

## Drum-Presets

| Preset | Gedacht für |
|---|---|
| Standard | Neutraler Startpunkt für den Drum-Bus |
| Rock – Punch | Band angefahren, Mitten aufgeräumt, Bus-Punch |
| Funk – trocken & tight | Schnelle Regelung, kurze Platte, viel Kontrolle |
| Vintage 70er | Warmes Band, gedämpfte Höhen, Snare-Platte |
| Parallel – fett | Parallel-Kompression am Bus |
| Ballade – groß | Lange Platte, offene Höhen |
| Lo-Fi / Demo | Band-Lo-Fi, Bandpass-Charakter |
| Snare – Einzelspur | Für die Snare-Spur: ohne Band, Platte an |
| Raummikro – zerquetscht | Raummikros hart komprimiert |

Alle Presets sind etwa gleich laut eingepegelt, damit der Vergleich fair bleibt.

## Installation

Am einfachsten über den [Klangbild Installer](https://github.com/Guitoli1/KlangbildInstaller):

```
curl -fsSL https://raw.githubusercontent.com/Guitoli1/KlangbildInstaller/main/install.sh | bash
```

Oder einzeln: unter **Actions** den letzten Build öffnen, Artefakt laden und
`Klangbild Drums.component` nach `~/Library/Audio/Plug-Ins/Components` kopieren.

## Build

JUCE 8 / CMake. GitHub Actions baut auf macOS 15 ein Universal Binary (arm64 + x86_64), signiert ad-hoc und prüft mit `auval`.
