# ESP32-C3 ↔ Toyota Techstream J2534 VIM

A complete, minimal Vehicle Interface Module (VIM) that lets **Toyota Techstream** talk ISO-15765 (CAN) through an **ESP32-C3** over Wi‑Fi.

Includes:
- **ESP32-C3 firmware** (TWAI/CAN + TCP server + UDP discovery + optional mDNS)
- **Windows J2534 DLL** that Techstream loads as a VIM (auto-discovers the ESP32)

> ⚠️ Use at your own risk. Vehicle comms and flashing can brick ECUs if misused.

---

## Features
- ISO-15765 (CAN) pass-through
- **Zero-config discovery**: DLL broadcasts `WICAN_DISCOVER`, device replies with `WICAN_OFFER <ip> <port>`
- Optional **mDNS** hostname `wican.local` + `_wican._tcp`
- Minimal J2534 API surface (open/close/connect/read/write) for Techstream basic ops
- Clean separation: J2534 specifics on PC, CAN I/O on ESP32

---

## Hardware
- **ESP32-C3** module + CAN transceiver (TJA1050/MCP2551 or 3.3V-safe equivalent)
- Default pins (changeable in firmware):
  - TWAI TX: **GPIO5**
  - TWAI RX: **GPIO4**
- Vehicle: OBD-II CAN (typically **500 kbit/s**)

---

## Build & Install

### 1) ESP32-C3 firmware
Toolchain: **Arduino-ESP32** (2.x)

Edit Wi‑Fi and (optionally) static IP in `esp32_firmware/esp32_wican_j2534.ino`:

```cpp
// Wi‑Fi
static const char* WIFI_SSID     = "YOUR_SSID";
static const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Optional static IP (before WiFi.begin(...))
IPAddress ip(192,168,1,50), gw(192,168,1,1), mask(255,255,255,0);
WiFi.config(ip, gw, gw, mask);

// Services
static const int   TCP_PORT  = 39424;   // J2534 DLL connects here
static const int   DISC_PORT = 53534;   // UDP discovery
static const char* MDNS_HOST = "wican"; // wican.local
```

**Flash the board. On boot it:**
- Starts a TCP server on `TCP_PORT`
- Listens for UDP discovery on `DISC_PORT`
- (Optional) Announces `wican.local` and `_wican._tcp`

### 2) Windows J2534 DLL
- Toolchain: **MSVC (x86)** — Techstream is 32-bit
- Build `windows_driver/wican_j2534.cpp` + `transport_client.cpp` into **wican_j2534.dll**
- Link: **Ws2_32.lib**
- Place DLL at:  
  `C:\Program Files\WiCAN\wican_j2534.dll` *(or Program Files (x86))*

### 3) Register the VIM
- Import `installer/wican_j2534.reg` to add J2534 registry keys.
- In Techstream, select **“WiCAN ESP32C3”** as the interface.

---

## Configuration

### Easiest (auto-discover): nothing to set
The DLL first checks env vars (below). If not set, it **broadcasts** on UDP/53534.  
The ESP32 replies with its **current DHCP IP** and port. The DLL connects automatically.

### Optional (pin a host/port)
Set once (machine-wide), then restart Techstream:

**Command Prompt**
```bat
setx WICAN_HOST 192.168.1.50 /M
setx WICAN_PORT 39424 /M
```

**PowerShell**
```powershell
[Environment]::SetEnvironmentVariable('WICAN_HOST','192.168.1.50','Machine')
[Environment]::SetEnvironmentVariable('WICAN_PORT','39424','Machine')
```

**Hardcode (optional)**
```cpp
// windows_driver/wican_j2534.cpp
static const char* HOST() { return "192.168.1.50"; }
static uint16_t    PORT() { return 39424; }
```

**mDNS option**
If Bonjour/mDNS is installed on Windows, you can set:
```
WICAN_HOST = wican.local
WICAN_PORT = 39424
```

---

## Usage
1. Plug the ESP32-C3 VIM into OBD-II and power it (vehicle or bench supply).
2. Confirm it joins your Wi‑Fi (LED/serial log).
3. Launch Techstream → choose **WiCAN ESP32C3** → connect to the vehicle.
4. The DLL will either use `WICAN_HOST/WICAN_PORT` or **auto-discover** the device.

---

## Troubleshooting
- **Techstream can’t connect**
  - Ensure the DLL is 32-bit and registered.
  - Allow local subnet **UDP/53534** and **TCP/39424** through the firewall.
  - Verify the ESP32 is on the same LAN and has an IP.
- **Discovery not working**
  - Temporarily set `WICAN_HOST` to the device IP.
  - Guest/VLAN/Wi‑Fi isolation can block broadcast; use mDNS (`wican.local`) or a DHCP reservation.
- **No CAN traffic**
  - Confirm 500 kbit/s bus, correct pins, proper CAN transceiver, and termination.

---

## Roadmap
- ISO‑TP segmentation on device for long frames
- Filters (hardware/software) and periodic messages
- Config GUI (scan & select device)
- Reverse‑connection mode (device dials out to PC)

---

## License
MIT (proposed). See `LICENSE`.

## Credits
Built for ESP32‑C3 TWAI + Techstream J2534 bridging; designed to be minimal, safe, and hackable.
