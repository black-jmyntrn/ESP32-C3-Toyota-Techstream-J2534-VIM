/*
 * ESP32-C3 WiCAN ↔ J2534 TCP bridge firmware (Arduino core)
 * Adds: UDP discovery responder + optional mDNS hostname (wican.local)
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <driver/twai.h>
#ifdef ARDUINO_ARCH_ESP32
  #include <ESPmDNS.h>
#endif

// ======== USER CONFIG ========
static const char* WIFI_SSID     = "YOUR_SSID";
static const char* WIFI_PASSWORD = "YOUR_PASSWORD";
static const int   TCP_PORT      = 39424;        // PC DLL connects here
static const int   DISC_PORT     = 53534;        // UDP discovery port
static const char* MDNS_HOST     = "wican";      // becomes wican.local

// CAN timing for 500 kbps on TWAI @ 80 MHz APB (typical)
static const twai_timing_config_t CAN_TIMING = TWAI_TIMING_CONFIG_500KBITS();
static const twai_filter_config_t CAN_FILTER = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_general_config_t CAN_GENERAL = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5 /*TX*/, GPIO_NUM_4 /*RX*/, TWAI_MODE_NORMAL);

// Packet types (PC <-> ESP)
enum PacketType : uint8_t { PKT_OPEN=0, PKT_CLOSE=1, PKT_SEND_FRAME=2, PKT_FRAME_RX=3, PKT_PING=4, PKT_SET_BITRATE=5, PKT_SET_FILTER=6 };

struct __attribute__((packed)) PktHdr {
  uint32_t len;  // payload length (type..data)
  uint8_t  type;
  uint8_t  ch;
  uint8_t  flags;
  uint8_t  dlc;
  uint32_t arb_id;
};

static const uint8_t FLAG_EXT = 0x01; // extended ID

WiFiServer server(TCP_PORT);
WiFiClient client;
WiFiUDP    udp;

static void send_frame_to_pc(uint32_t arb_id, const uint8_t* data, uint8_t dlc, bool isExt) {
  if (!client.connected()) return;
  PktHdr hdr;
  hdr.type   = PKT_FRAME_RX;
  hdr.ch     = 0;
  hdr.flags  = isExt ? FLAG_EXT : 0;
  hdr.dlc    = dlc;
  hdr.arb_id = arb_id;
  hdr.len    = sizeof(PktHdr) - sizeof(uint32_t) + dlc;
  uint32_t payloadLen = hdr.len;
  client.write((uint8_t*)&payloadLen, 4);
  client.write(((uint8_t*)&hdr)+4, sizeof(PktHdr)-4);
  if (dlc) client.write(data, dlc);
}

static bool can_init() {
  if (twai_driver_install(&CAN_GENERAL, &CAN_TIMING, &CAN_FILTER) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  return true;
}

static void can_shutdown() {
  twai_stop();
  twai_driver_uninstall();
}

static void handle_incoming_packets() {
  while (client.connected() && client.available() >= 4) {
    uint32_t len = 0; client.read((uint8_t*)&len, 4);
    if (len < (sizeof(PktHdr)-4)) { continue; }
    uint8_t buf[4 + sizeof(PktHdr) - 4 + 64];
    int toRead = len; int got = client.read(buf, toRead);
    if (got != toRead) { return; }
    PktHdr* hdr = (PktHdr*)buf;
    switch (hdr->type) {
      case PKT_CLOSE: { client.stop(); break; }
      case PKT_PING:  { uint32_t L=len; client.write((uint8_t*)&L,4); client.write(buf,len); break; }
      case PKT_SET_BITRATE: { /* fixed 500k for minimal build */ break; }
      case PKT_SEND_FRAME: {
        twai_message_t msg = {};
        bool ext = (hdr->flags & FLAG_EXT) != 0;
        msg.identifier = hdr->arb_id & (ext ? 0x1FFFFFFF : 0x7FF);
        msg.extd = ext; msg.rtr = 0; msg.data_length_code = hdr->dlc;
        if (hdr->dlc) memcpy(msg.data, buf + (sizeof(PktHdr) - 4), hdr->dlc);
        twai_transmit(&msg, pdMS_TO_TICKS(20));
        break;
      }
      default: break;
    }
  }
}

static void pump_can_to_pc() {
  twai_message_t msg;
  while (twai_receive(&msg, 0) == ESP_OK) {
    send_frame_to_pc(msg.identifier, msg.data, msg.data_length_code, msg.extd);
  }
}

static void handle_discovery() {
  int pkt = udp.parsePacket();
  if (!pkt) return;
  char buf[64]; int n = udp.read(buf, sizeof(buf));
  if (n<=0) return;
  // Protocol: request "WICAN_DISCOVER" -> reply with "WICAN_OFFER ip port"
  if (n>=14 && strncmp(buf, "WICAN_DISCOVER", 14)==0) {
    IPAddress ip = WiFi.localIP();
    char reply[64];
    snprintf(reply, sizeof(reply), "WICAN_OFFER %u.%u.%u.%u %d", ip[0], ip[1], ip[2], ip[3], TCP_PORT);
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t*)reply, strlen(reply));
    udp.endPacket();
  }
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(300); }

  #ifdef ARDUINO_ARCH_ESP32
    if (MDNS.begin(MDNS_HOST)) {
      MDNS.addService("wican", "_tcp", TCP_PORT); // optional
    }
  #endif

  udp.begin(DISC_PORT);

  if (!can_init()) { while (true) { delay(1000); } }
  server.begin();
}

void loop() {
  if (!client || !client.connected()) { client = server.available(); }
  if (client && client.connected()) {
    handle_incoming_packets();
    pump_can_to_pc();
  } else {
    delay(10);
  }
  handle_discovery();
}
