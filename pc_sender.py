"""
ETS2 -> ESP32 TFT Dashboard - PC gonderici script (sadece hiz/rpm/vites)

Kurulum:
    pip install requests

Onceden yapman gerekenler:
    1) "ETS2 Telemetry Server" (Funbit) kur ve calistir:
       https://github.com/Funbit/ets2-telemetry-server
       Kurulumdan sonra tarayicidan http://localhost:25555 acip
       telemetri verisinin geldigini gor.
    2) ESP32 kartina firmware'i yukle, Seri Monitor'den "Ev WiFi'ye baglandi, IP:"
       satirindaki adresi al (telefonun baglandigi AP IP'si DEGIL, ev WiFi'deki IP).
    3) Asagida ESP32_IP degiskenini o IP ile degistir. PC de ayni ev WiFi agina
       bagli olmali (ESP32'nin telefon icin actigi ayri agdan degil).
    4) python pc_sender.py ile calistir (ETS2 acikken).
"""

import socket
import struct
import time
import json
import requests

# ---------- AYARLAR ----------
ESP32_IP = "192.168.1.12"   # ESP32'nin seri monitorde gordugun IP'si
ESP32_PORT = 5000

TELEMETRY_URL = "http://localhost:25555/api/ets2/telemetry"
SEND_HZ = 10                 # saniyede kac kez veri gonderilsin
# -------------------------------

TYPE_TELEMETRY = 0x01


def send_frame(sock, msg_type, payload: bytes):
    header = struct.pack(">B I", msg_type, len(payload))
    sock.sendall(header + payload)


def get_telemetry():
    try:
        r = requests.get(TELEMETRY_URL, timeout=0.5)
        data = r.json()
        speed = int(round(data["truck"]["speed"]["kph"]))
        rpm = int(round(data["truck"]["engineRpm"]["value"]))
        gear = int(data["truck"]["transmission"]["gear"])
        return {"speed": speed, "rpm": rpm, "gear": gear}
    except Exception as e:
        print(f"[uyari] telemetri okunamadi: {e}")
        return None


def connect():
    while True:
        try:
            print(f"ESP32'ye baglaniliyor: {ESP32_IP}:{ESP32_PORT}")
            s = socket.create_connection((ESP32_IP, ESP32_PORT), timeout=5)
            print("Baglanti kuruldu.")
            return s
        except OSError as e:
            print(f"Baglanamadi ({e}), 3 sn sonra tekrar denenecek...")
            time.sleep(3)


def main():
    sock = connect()
    interval = 1.0 / SEND_HZ

    while True:
        start = time.time()

        t = get_telemetry()
        if t:
            try:
                send_frame(sock, TYPE_TELEMETRY, json.dumps(t).encode())
            except (BrokenPipeError, ConnectionResetError, OSError):
                print("ESP32 baglantisi kesildi, yeniden baglaniliyor...")
                sock = connect()

        elapsed = time.time() - start
        time.sleep(max(0.0, interval - elapsed))


if __name__ == "__main__":
    main()
