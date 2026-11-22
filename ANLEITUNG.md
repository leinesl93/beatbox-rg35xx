# Beatbox für den Anbernic RG35XX (MinUI) – Anleitung

**Wichtig vorab:** Ich konnte das Programm hier nur am Rechner testen (Klang, alle 10 Kits, Effekte, Bedienung, Bildschirme). Das Bauen für das Gerät und der Start auf dem RG35XX selbst sind **nicht** getestet – dafür habe ich kein Gerät und keine ARM-Umgebung. Der Weg unten ist so gebaut, dass Fehler sichtbar werden. Wenn etwas hakt, schick mir die Fehlermeldung oder die Datei `beatbox.log`, dann korrigiere ich es.

## Was du brauchst
- Einen Computer (zum Bauen und zum Kopieren auf die SD-Karte)
- Einen kostenlosen GitHub-Account (github.com)
- Die SD-Karte mit MinUI aus dem RG35XX

## Schritt 1 – Programm bauen (über GitHub, ohne Installation)
Das Programm muss für den ARM-Prozessor des RG35XX übersetzt werden. GitHub erledigt das kostenlos im Browser.

1. Entpacke `beatbox-rg35xx.zip` auf deinem Computer.
2. Auf github.com: **New repository** → Name z. B. `beatbox-rg35xx` → **Public** (nur öffentliche Repositories bekommen den kostenlosen ARM-Rechner) → **Create repository**.
3. Im neuen Repository: **uploading an existing file**. Ziehe den **gesamten Inhalt** des entpackten Ordners hinein: `src`, `pak`, `Makefile`, `build_rg35xx.sh`, `ANLEITUNG.md` und den versteckten Ordner `.github`.
   - Am Mac: im Finder mit **Cmd+Shift+.** versteckte Ordner einblenden.
   - Klappt der Upload von `.github` nicht: im Repository **Add file → Create new file**, als Namen `.github/workflows/build.yml` eintippen (die Schrägstriche erzeugen die Ordner) und den Inhalt der Datei aus dem Zip einfügen.
4. **Commit changes** klicken.
5. Oben auf **Actions** gehen. Ein Lauf „Beatbox für RG35XX bauen“ startet von selbst und dauert etwa 3–5 Minuten (der erste Download der Toolchain ist ca. 1,2 GB).
6. Grüner Haken: Lauf anklicken, unten bei **Artifacts** `Beatbox-pak` herunterladen und entpacken. Darin liegt der Ordner **Beatbox.pak**.
7. Roter Haken: Lauf anklicken, den fehlgeschlagenen Schritt aufklappen und mir den Text schicken (oder einen Screenshot).

*Alternative mit Docker:* Wenn du Docker hast, reicht im entpackten Ordner `./build_rg35xx.sh`. Das Ergebnis liegt dann in `dist/Beatbox.pak`.

## Schritt 2 – Auf die SD-Karte kopieren
1. SD-Karte mit MinUI in den Computer stecken.
2. Öffne den Ordner **Tools**. Dort gibt es einen Unterordner für dein Gerät (Plattform-Ordner, z. B. `rg35xx`). Der richtige ist der, in dem schon **Clock.pak** liegt.
3. Kopiere **Beatbox.pak** genau dorthin, neben `Clock.pak`.
4. SD-Karte sicher auswerfen und wieder einsetzen.

Hinweis: Wenn es keinen `Tools`-Ordner gibt, lege ihn im Hauptverzeichnis der Karte an und darin den Plattform-Ordner. Die Ordnernamen sollten wie bei den anderen Tools geschrieben sein.

## Schritt 3 – Starten und Tasten einlernen
1. RG35XX einschalten, in MinUI **Tools → Beatbox** wählen.
2. Beim ersten Start fragt das Programm nacheinander **alle 13 Tasten** ab (Hoch, Runter, Links, Rechts, A, B, X, Y, L, R, Select, Start, Menu). Drücke jeweils die genannte Taste. Das passiert nur einmal; die Belegung wird in `bbmap.cfg` gespeichert.
3. Danach erscheint das Raster mit dem Kit HIPHOP. **START** startet den Beat.

Falsche Taste eingelernt? SELECT + MENU drücken (lernt neu) oder `bbmap.cfg` im Pak-Ordner löschen.

## Steuerung
**Raster**

| Taste | Funktion |
|---|---|
| Steuerkreuz | Cursor bewegen |
| A | Step: aus → an → Akzent → aus |
| B + Hoch/Runter | Lautstärke der Spur |
| B + Links/Rechts | Tonhöhe der Spur |
| X | Spur stumm |
| Y | Zeile leeren |
| L / R | Pattern A–D (beim Spielen am Taktende) |
| START | Play / Stop |
| SELECT + Hoch/Runter | Tempo (BPM) |
| SELECT + Links/Rechts | Swing |
| SELECT + L/R | Kit wechseln (HIPHOP, TECHNO, LOFI, HOUSE, TRAP, DNB, ELECTRO, DUB, SYNTHW, DISCO) |
| SELECT + A | Beat des Kits neu laden |
| SELECT + Y | ganzes Pattern leeren |
| SELECT + START | Effekt-Seite |
| MENU | Speichern und beenden |

**Effekt-Seite** (wirkt auf den ganzen Beat): Hoch/Runter wählt den Parameter, Links/Rechts ändert den Wert, A schaltet alle Effekte an/aus, Y setzt zurück, SELECT + START geht zurück. Parameter: Filter (Tiefpass ↔ Hochpass, Mitte = offen), Resonanz, Delay (Mix, Zeit, Feedback), Hall (Mix, Größe), Drive, Crush.

Dein Beat, Tempo, Kit und Effekte werden beim Beenden und alle 20 Sekunden bei Änderungen in `bbsave.dat` gespeichert und beim nächsten Start geladen.

## Wenn etwas nicht klappt
| Problem | Was tun |
|---|---|
| Actions-Lauf rot | Text des fehlgeschlagenen Schritts an mich schicken. Der Diagnose-Schritt zeigt, welche Toolchain-Pfade vorhanden sind. |
| Beatbox fehlt im Menü | Ordner liegt am falschen Ort. Er muss im selben Ordner wie `Clock.pak` liegen und `Beatbox.pak` heißen. |
| Schwarzer Bildschirm / Rückkehr ins Menü | Datei `beatbox.log` im Pak-Ordner (auf der SD-Karte) öffnen und mir den Inhalt schicken. |
| Bild da, kein Ton | Lautstärke am Gerät prüfen. Steht im Log „Audio konnte nicht geöffnet werden“, schick mir die Zeile. |
| Tasten reagieren nicht / falsch | `bbmap.cfg` löschen und neu einlernen. |
| Knackser oder Ruckeln | Sag mir Bescheid; ich kann die Klangqualität dann etwas senken (z. B. 22 kHz) oder den Puffer vergrößern. |

## Was getestet ist und was nicht
- **Getestet (am Rechner):** alle 10 Kits und 12 Spuren erzeugen Klang ohne Übersteuern, Effekte inklusive Extremwerte, Bedienlogik, Speichern/Laden, Tasten-Einlernen, Bildschirme. Rechenaufwand im ungünstigsten Fall rund 30-fach schneller als Echtzeit auf einem normalen PC.
- **Nicht getestet:** Bauen mit der MinUI-Toolchain, Start unter MinUI auf dem echten RG35XX, Tastencodes und Audioausgabe des Geräts, Rechenleistung auf dem ARM-Prozessor.
