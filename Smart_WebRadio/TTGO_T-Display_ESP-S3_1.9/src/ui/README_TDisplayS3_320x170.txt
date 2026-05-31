Celeszkoz: LILYGO T-Display S3, 320x170 px.

Az Arduino IDE minden .cpp fajlt lefordit a sketch mappaban, akkor is, ha nincs include-olva.
Ezert a regi ui_320x240.cpp ki lett kapcsolva .disabled kiterjesztessel, mert ugyanazokat a ui320... fuggvenyneveket tartalmazta, mint az uj ui_320x170.cpp.

Aktiv UI fajlok:
- ui_320x170.h
- ui_320x170.cpp

A ui_320x240.* csak archivumkent maradt meg, nem fordul.
