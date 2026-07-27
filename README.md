# ESP32 Direksiyon Simulatoru

ETS2 (Euro Truck Simulator 2) icin wifi tabanli direksiyon simulatoru.

## Ozellikler

- **TFT Dashboard**: 1.8" ekran uzerinde gercek hiz, RPM ve vites gosterimi
- **Telefon Kontrolu**: WiFi AP uzerinden telefon tarayicisından direksiyon/gaz/fren kontrolu
- **USB Gamepad**: PC'ye takinca Xbox gibi joystick olarak calisir
- **ETS2 Telemetri**: Gercek oyun verilerini TCP uzerinden alir
- **Titresim Geri Bildirimi**: Redline yaklasinca telefon titresimi
- **Acil Durma**: WiFi bagiantisi kopunca otomatik F1 tusu basma

## Kurulum

### Arduino IDE

1. Board: **ESP32S3 Dev Module** sec
2. USB Mode: **USB-OTG (TinyUSB)** sec
3. USB CDC On Boot: **Enabled** sec

### Kutuphaneler

- TFT_eSPI (Bodmer)
- ESPAsyncWebServer + AsyncTCP
- ArduinoJson

### Kod

1. `steering_sim_full.ino` dosyasini Arduino IDE'ye yukle
2. WiFi bilgilerini degistir (SSID/PASS)
3. Derle ve yükle

### PC Python Scripti

```bash
pip install requests
python pc_sender.py
```

Oncesinde:
1. ETS2 Telemetry Server kur: https://github.com/Funbit/ets2-telemetry-server
2. `pc_sender.py`'deki ESP32_IP'yi seri monitorden gelen IP ile degistir
3. ETS2'yi ac ve script'i calistir

## Kullanim

### Telefon

1. WiFi: `SteeringWheelSim` / `12345678` baglantisi yap
2. Tarayici: `http://192.168.4.1` ac
3. Telefonun egim sensoru direksiyon olur
4. Ekrandaki butonlarla kontrol et

### ETS2

- Ayarlar > Kontroller > Vites kutusu: **Sequential** sec
- Direksiyon/Gaz/Fren otomatik eslenir
- Vites +/- butonlari otomatik eslenir

## TFT Pinleri

```
TFT_CS   = GPIO10
TFT_DC   = GPIO9
TFT_RST  = GPIO8
TFT_MOSI = GPIO11
TFT_SCLK = GPIO12
```

## Sorun Giderme

**Seri Monitor hata mesaji**: Arduino IDE'de Seri Monitor'u ac (115200 baud)

**WiFi baglantisi yok**: SSID/PASS'i kontrol et, WiFi router'i restart et

**Telemetri gelmiyor**: ETS2 Telemetry Server'i ac, `python pc_sender.py`'i calistir

**USB Gamepad tanini gormuyor**: USB Mode'u kontrol et, Arduino IDE restart et
