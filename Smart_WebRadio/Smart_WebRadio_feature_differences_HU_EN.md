# Simple / Smart WebRadio – Funkciókülönbségek  
# Simple / Smart WebRadio – Feature Differences

Ez a dokumentum az eredeti minimalista WebRadio firmware és a jelenlegi Smart WebRadio állapot közti főbb tudás- és funkciókülönbségeket foglalja össze.

This document summarizes the main feature and capability differences between the original minimal WebRadio firmware and the current Smart WebRadio version.

---

## HU – Funkciókülönbségek

### Eredeti állapot

Az eredeti kód egy egyszerű, minimalista internetes rádió volt.

Főbb jellemzői:

- Radio Browser alapú online állomáskeresés
- országválasztás
- műfajválasztás
- állomásléptetés
- hangerőállítás
- egyszerű ST7735 kijelzős felület
- `Now Playing` jellegű alap kijelzés
- alap gomb / rotary encoder kezelés
- online állomások közvetlen lejátszása
- Preferences alapú egyszerű mentés
- nem használt aktívan SPIFFS/LittleFS tartalmat

A készülék működött rádióként, de erősen függött az online Radio Browser forrástól, és nem volt saját, helyben kezelhető állomáslistája.

---

### Jelenlegi állapot

A mostani verzió már egy jóval komplettabb, önállóbb kis webrádió firmware.

Új / bővített funkciók:

---

### 1. Local állomáslista SPIFFS-ről

- `/stations.txt` fájl használata SPIFFS partícióról
- myRadio kompatibilis állomáslista formátum támogatása:

```txt
Állomás neve<TAB>Stream URL<TAB>opcionális logo mező
```

- a harmadik oszlopot figyelmen kívül hagyja, így myRadio listákkal is kompatibilis
- Local mód országként / forrásként választható
- induláskor Local állomáslistáról tud indulni
- nem kell újrafordítani a firmware-t állomáslista módosításkor

---

### 2. Partition Manager kompatibilitás

- bekerült a soros MRSPIFS protokoll támogatása
- a saját LittleFS-SPIFFS Partition Managerrel elérhető a rádió SPIFFS partíciója - https://github.com/gidano/myRadio-SPIFFS-Manager
- fájlok listázása, írása, olvasása, törlése USB soros kapcsolaton keresztül
- `/stations.txt` frissíthető külön ESPConnect nélkül
- a normál rádió funkciók megtartása mellett működik

Támogatott soros műveletek:

```txt
MRSPIFS:BEGIN
PING
LIST
READ
WRITE_BEGIN
WRITE_DATA
WRITE_END
DELETE
REBOOT
```

---

### 3. Webes stations.txt kezelés

- a webes állomáskezelés is megmaradt
- a Local lista nem csak sorosan, hanem webes úton is kezelhető
- megmaradtak az alap endpointok, például:

```txt
/api/stations
/api/ip
/api/reload-local
```

---

### 4. Local lapozás

- a hosszabb `stations.txt` lista már lapozható
- `Long = Next Page` Local módban is működik
- 30-as oldalanként tud haladni
- az utolsó oldal után visszaugrik az első Local oldalra
- online Radio Browser lapozás is megmaradt

---

### 5. Metaadat kijelzés

- működő ICY / stream metadata támogatás
- állomásnév, előadó és dalcím külön sorban jelenik meg
- a korábbi `Now Playing` szöveg helyére az aktuális állomásnév került
- az előadó és dalcím alatta jelenik meg két külön sorban
- az újabb `ESP32-audioI2S` callback rendszer támogatása is bekerült

---

### 6. Ékezetkezelés / ASCII konverzió

- a meglévő `asciiText()` jellegű konverzió a metadata szövegekre is használva lett
- így az ékezetes karakterek nem rontják el a kis kijelzős megjelenítést
- egyszerűbb, stabilabb ST7735 kompatibilis szövegkirajzolás

---

### 7. Hosszú szövegek scrollozása

- állomásnév / előadó / dalcím scroll támogatás
- csak az a sor mozog, amelyik hosszabb a rendelkezésre álló helynél
- egyszerre csak egy sor scrollozhat, erőforráskímélés miatt
- a hosszú szöveg egyszer végigfut
- utána 5 másodperc szünet
- majd a következő hosszú sor következik
- a sorok belső clippinggel rajzolódnak, így nem lógnak ki a keretből

---

### 8. Bitráta és kodek kijelzés

- az alsó country és genre gomb közé bekerült egy középső státusz mező
- kiírja a bitrátát és a használt kodeket
- példák:

```txt
192k MP3
128k AAC
1.4M FLAC
```

- 1000 kbps felett M formátumra vált
- FLAC esetén képes nagyobb bitráta kijelzésére is
- codec felismerés audio callbackből, URL-ből és audio lib lekérdezésből

---

### 9. Jobb UI elrendezés

- ország gomb bal oldalon
- műfaj gomb jobb oldalon
- bitrate/codec mező középen
- hosszú nyomás indikátor kör átkerült a felső Volume sor jobb végére
- hangerő pöttyök közelebb kerültek a `Volume` felirathoz
- a kijelző alsó része tisztább lett

---

### 10. Gyorsított állomásléptetés

- `+` és `-` gomb röviden továbbra is egyet lép
- hosszan nyomva tartva automatikusan ismétel
- az ismétlés fokozatosan gyorsul
- gyorsabb nagy állomáslistában való navigáció
- rotary encoder működése nem lett megváltoztatva

---

### 11. Gombkezelés megtartása

- a korábban jól működő gomblogika megmaradt
- menü gomb továbbra is nyugtázásra használható
- hosszú menü gomb továbbra is a `Next Page` funkciót kezeli
- Local és online mód között váltva is megmaradt a helyes működés

---

### Rövid HU összegzés

Az eredeti rádió egy egyszerű online Radio Browser kliens volt.  
A mostani verzió már egy önállóbb, helyben is menedzselhető, SPIFFS-alapú, metadata-kijelzős, saját állomáslistás kis webrádió, amely megőrizte az eredeti minimalista karakterét, de sokkal használhatóbb lett napi használatra.

---

## EN – Feature Differences

### Original State

The original code was a simple, minimal internet radio.

Main features:

- Radio Browser based online station search
- country selection
- genre selection
- station switching
- volume control
- simple ST7735 display UI
- basic `Now Playing` style display
- basic button / rotary encoder handling
- direct online stream playback
- simple Preferences based state saving
- no active SPIFFS/LittleFS based station handling

The device worked as a radio, but it depended strongly on the online Radio Browser source and had no locally manageable station list.

---

### Current State

The current version is a much more complete and standalone small webradio firmware.

New / extended features:

---

### 1. Local Station List from SPIFFS

- supports `/stations.txt` stored on the SPIFFS partition
- compatible with the myRadio station list format:

```txt
Station name<TAB>Stream URL<TAB>optional logo field
```

- the third column is ignored, so existing myRadio lists can be reused
- Local mode can be selected as a source/country option
- the radio can start directly from the Local station list
- station updates no longer require recompiling the firmware

---

### 2. Partition Manager Compatibility

- serial MRSPIFS protocol support was added
- the radio’s SPIFFS partition can be accessed with the custom LittleFS-SPIFFS Partition Manager
- files can be listed, read, written and deleted through USB serial
- `/stations.txt` can be updated without using ESPConnect
- normal radio operation was kept intact

Supported serial commands:

```txt
MRSPIFS:BEGIN
PING
LIST
READ
WRITE_BEGIN
WRITE_DATA
WRITE_END
DELETE
REBOOT
```

---

### 3. Web-based stations.txt Handling

- web-based station handling was kept
- the Local station list can be managed both through serial and web access
- existing basic endpoints remained available, for example:

```txt
/api/stations
/api/ip
/api/reload-local
```

---

### 4. Local Paging

- long `/stations.txt` lists can now be paged
- `Long = Next Page` also works in Local mode
- Local stations are loaded in pages of 30 entries
- after the last Local page, it wraps back to the first page
- online Radio Browser paging was preserved

---

### 5. Metadata Display

- working ICY / stream metadata support
- station name, artist and title are displayed separately
- the old `Now Playing` label was replaced by the current station name
- artist and song title are shown below it in two separate lines
- support for the newer `ESP32-audioI2S` callback system was added

---

### 6. Accent / ASCII Conversion

- the existing `asciiText()` style conversion is also used for metadata text
- accented characters no longer break the small display layout
- simpler and more stable ST7735-compatible text rendering

---

### 7. Scrolling Long Text

- station name / artist / title scrolling support
- only lines that are longer than the available space scroll
- only one text line scrolls at a time to save resources
- the long text scrolls once
- then waits for 5 seconds
- then the next long line is scrolled
- text is clipped inside its own field, so it no longer overflows the frame

---

### 8. Bitrate and Codec Display

- a new middle status chip was added between the country and genre buttons
- it displays bitrate and codec
- examples:

```txt
192k MP3
128k AAC
1.4M FLAC
```

- above 1000 kbps it switches to M format
- FLAC and high-bitrate streams are handled better
- codec detection uses callback data, URL hints and audio library polling

---

### 9. Improved UI Layout

- country button on the left
- genre button on the right
- bitrate/codec status field in the center
- the long-press progress circle was moved to the right end of the top Volume row
- volume dots were moved closer to the `Volume` label
- the lower UI area became cleaner

---

### 10. Fast Station Stepping

- short `+` / `-` press still steps one station
- long press starts automatic repeat
- repeat speed gradually increases
- browsing large station lists is much faster
- rotary encoder behavior was not changed

---

### 11. Preserved Button Handling

- the previously working button behavior was preserved
- menu button still confirms selections
- long menu press still handles the `Next Page` function
- switching between Local and online sources keeps the expected behavior

---

### Short EN Summary

The original radio was a simple online Radio Browser client.  
The current version is a much more independent SPIFFS-based webradio with a local station list, metadata display, Partition Manager support, web station management, better navigation, scrolling text, bitrate/codec display and a more polished UI — while still keeping the original minimalist character.
