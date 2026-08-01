#include "boards.h"
#include <RadioLib.h>

// ================= RADIO INSTANCE =================
SX1262 radio =
    new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// ================= FLAGS =================
volatile bool receivedFlag = false;

// =============================================================================
// LoRaMesher Wire-Format Structs
// All multi-byte fields are little-endian (matches LoRaMesher serializer).
// =============================================================================

// LoRaMesher BaseHeader — 6 bytes, present on every frame
struct LMBaseHeader {
  uint16_t destination; // Final destination address
  uint16_t source;      // Originating node address
  uint8_t message_type; // MessageType enum value (see below)
  uint8_t payload_size; // Bytes following this header
};

// LoRaMesher DataHeader extension — 4 bytes, appended after BaseHeader for type
// 0x11 / 0x12
struct LMDataHeaderExt {
  uint16_t next_hop; // Immediate next hop address
  uint8_t ttl;       // Time-to-live (decremented each hop)
  uint8_t seq_num;   // Mesh-level per-source sequence number (de-duplication)
};

// Testbed PerformancePayload — sent inside DATA frames with app_msg_type ==
// 0x15 Preceded by a single app_msg_type byte within the application payload.
#pragma pack(push, 1)
struct PerformancePayload {
  uint32_t
      sequence_number; // Monotonically increasing app-level sequence number
  uint32_t sender_timestamp; // millis() at packet creation on the end-node
  uint16_t generation_rate;  // Current packet generation interval (ms)
  uint8_t payload_size;      // Size of the dummy padding region
  uint8_t padding[32];       // Dummy padding bytes (seeded with 0xAA)
};

// Testbed ConfigBroadcastPayload — sent inside BROADCAST frames with
// app_msg_type == 0x16
struct ConfigBroadcastPayload {
  uint16_t new_generation_rate_ms; // New interval all end-nodes should adopt
};
#pragma pack(pop)

// =============================================================================
// LoRaMesher MessageType constants (from message_type.hpp)
// =============================================================================
// Data messages (0x1x)
#define LM_DATA 0x11
#define LM_DATA_BROADCAST 0x12
#define LM_AODV_DATA 0x13
// Control messages (0x2x)
#define LM_ACK 0x21
#define LM_PING 0x23
#define LM_PONG 0x24
// Routing messages (0x3x)
#define LM_HELLO 0x31
#define LM_ROUTE_TABLE 0x32
#define LM_AODV_RREQ 0x33
#define LM_AODV_RREP 0x34
#define LM_AODV_RERR 0x35
#define LM_AODV_RREP_ACK 0x36
// System messages (0x4x)
#define LM_SYNC 0x41
#define LM_JOIN_REQUEST 0x42
#define LM_JOIN_RESPONSE 0x43
#define LM_SLOT_REQUEST 0x44
#define LM_SLOT_ALLOC 0x45
#define LM_SYNC_BEACON 0x46
#define LM_NM_CLAIM 0x47

// Testbed app-layer message types (first byte of the application payload)
#define APP_PERF_DATA 0x15
#define APP_UPDATE_CONFIG 0x16

// =============================================================================
// Utility: print a hex dump of a byte buffer
// =============================================================================
void hexDump(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10)
      Serial.print("0");
    Serial.print(buf[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0)
      Serial.println();
  }
  if (len % 16 != 0)
    Serial.println();
}

// =============================================================================
// Utility: human-readable message type name
// =============================================================================
const char *msgTypeName(uint8_t t) {
  switch (t) {
  case LM_DATA:
    return "DATA (unicast)";
  case LM_DATA_BROADCAST:
    return "DATA_BROADCAST";
  case LM_AODV_DATA:
    return "AODV_DATA";
  case LM_ACK:
    return "ACK";
  case LM_PING:
    return "PING";
  case LM_PONG:
    return "PONG";
  case LM_HELLO:
    return "HELLO";
  case LM_ROUTE_TABLE:
    return "ROUTE_TABLE";
  case LM_AODV_RREQ:
    return "AODV_RREQ";
  case LM_AODV_RREP:
    return "AODV_RREP";
  case LM_AODV_RERR:
    return "AODV_RERR";
  case LM_AODV_RREP_ACK:
    return "AODV_RREP_ACK";
  case LM_SYNC:
    return "SYNC";
  case LM_JOIN_REQUEST:
    return "JOIN_REQUEST";
  case LM_JOIN_RESPONSE:
    return "JOIN_RESPONSE";
  case LM_SLOT_REQUEST:
    return "SLOT_REQUEST";
  case LM_SLOT_ALLOC:
    return "SLOT_ALLOCATION";
  case LM_SYNC_BEACON:
    return "SYNC_BEACON";
  case LM_NM_CLAIM:
    return "NM_CLAIM";
  default:
    return "UNKNOWN";
  }
}

// =============================================================================
// Deep decoder: LoRaMesher DATA / DATA_BROADCAST application payload
// Parses the single app_msg_type byte and the struct that follows it.
// =============================================================================
void decodeAppPayload(const uint8_t *app_buf, size_t app_len) {
  if (app_len < 1) {
    Serial.println("  [App Payload] Empty.");
    return;
  }

  uint8_t app_msg_type = app_buf[0];

  if (app_msg_type == APP_PERF_DATA) {
    // ---- Testbed PerformancePayload ----
    if (app_len < 1 + sizeof(PerformancePayload)) {
      Serial.print("  [App Payload] PERF_DATA (0x15) too short (");
      Serial.print(app_len);
      Serial.println(" bytes)");
      return;
    }
    PerformancePayload p;
    memcpy(&p, app_buf + 1, sizeof(PerformancePayload));

    Serial.println("  [App Payload] PERF_DATA (0x15) — PerformancePayload:");
    Serial.print("    Seq Number   : ");
    Serial.println(p.sequence_number);
    Serial.print("    Tx Timestamp : ");
    Serial.print(p.sender_timestamp);
    Serial.println(" ms");
    Serial.print("    Gen Rate     : ");
    Serial.print(p.generation_rate);
    Serial.println(" ms");
    Serial.print("    Padding Size : ");
    Serial.print(p.payload_size);
    Serial.println(" bytes");
    Serial.print("    Padding[0]   : 0x");
    Serial.println(p.padding[0], HEX);

  } else if (app_msg_type == APP_UPDATE_CONFIG) {
    // ---- Testbed ConfigBroadcastPayload ----
    if (app_len < 1 + sizeof(ConfigBroadcastPayload)) {
      Serial.print("  [App Payload] UPDATE_CONFIG (0x16) too short (");
      Serial.print(app_len);
      Serial.println(" bytes)");
      return;
    }
    ConfigBroadcastPayload c;
    memcpy(&c, app_buf + 1, sizeof(ConfigBroadcastPayload));

    Serial.println(
        "  [App Payload] UPDATE_CONFIG (0x16) — ConfigBroadcastPayload:");
    Serial.print("    New Gen Rate : ");
    Serial.print(c.new_generation_rate_ms);
    Serial.println(" ms");

  } else {
    // Unknown app-layer type — hex dump so nothing is lost
    Serial.print("  [App Payload] Unknown app type 0x");
    Serial.print(app_msg_type, HEX);
    Serial.print(" (");
    Serial.print(app_len);
    Serial.println(" bytes) — raw dump:");
    hexDump(app_buf, app_len);
  }
}

// =============================================================================
// Main decoder: parse a raw LoRaMesher frame and pretty-print it
// =============================================================================
void decodeLoRaMesherPacket(const uint8_t *buf, size_t len) {
  unsigned long ts = millis();

  Serial.println();
  Serial.println(
      "=================== LORAMESHER PACKET INTERCEPTED ===================");
  Serial.print("Timestamp    : ");
  Serial.print(ts);
  Serial.println(" ms");
  Serial.print("Frame Length : ");
  Serial.print(len);
  Serial.println(" bytes");
  Serial.print("RSSI         : ");
  Serial.print(radio.getRSSI());
  Serial.println(" dBm");
  Serial.print("SNR          : ");
  Serial.print(radio.getSNR());
  Serial.println(" dB");
  Serial.println(
      "----------------------------------------------------------------------");

  // ---- Validate minimum BaseHeader (6 bytes) ----
  if (len < sizeof(LMBaseHeader)) {
    Serial.println(
        "[ERROR] Frame too short for a valid LoRaMesher BaseHeader.");
    hexDump(buf, len);
    Serial.println("==========================================================="
                   "===========");
    return;
  }

  // ---- Parse BaseHeader (little-endian, packed) ----
  LMBaseHeader base;
  memcpy(&base, buf, sizeof(LMBaseHeader));

  Serial.print("Destination  : 0x");
  Serial.println(base.destination, HEX);
  Serial.print("Source       : 0x");
  Serial.println(base.source, HEX);
  Serial.print("Msg Type     : 0x");
  Serial.print(base.message_type, HEX);
  Serial.print(" (");
  Serial.print(msgTypeName(base.message_type));
  Serial.println(")");
  Serial.print("Payload Size : ");
  Serial.print(base.payload_size);
  Serial.println(" bytes");

  bool is_data_type =
      (base.message_type == LM_DATA || base.message_type == LM_DATA_BROADCAST ||
       base.message_type == LM_AODV_DATA);

  // ---- For DATA types: parse the DataHeader extension (4 bytes) ----
  if (is_data_type) {
    size_t data_hdr_end = sizeof(LMBaseHeader) + sizeof(LMDataHeaderExt);
    if (len < data_hdr_end) {
      Serial.println("[ERROR] DATA frame too short for DataHeader extension.");
      hexDump(buf, len);
      Serial.println("========================================================="
                     "=============");
      return;
    }

    LMDataHeaderExt dhx;
    memcpy(&dhx, buf + sizeof(LMBaseHeader), sizeof(LMDataHeaderExt));

    Serial.println("-----------------------------------------------------------"
                   "-----------");
    Serial.print("Next Hop     : 0x");
    Serial.println(dhx.next_hop, HEX);
    Serial.print("TTL          : ");
    Serial.println(dhx.ttl);
    Serial.print("Mesh Seq#    : ");
    Serial.println(dhx.seq_num);

    // ---- Decode application payload ----
    const uint8_t *app_start = buf + data_hdr_end;
    size_t app_len = (len > data_hdr_end) ? (len - data_hdr_end) : 0;

    Serial.println("-----------------------------------------------------------"
                   "-----------");
    decodeAppPayload(app_start, app_len);

  } else {
    // ---- Non-data message: hex-dump the payload for inspection ----
    Serial.println("-----------------------------------------------------------"
                   "-----------");
    size_t ctrl_offset = sizeof(LMBaseHeader);
    size_t ctrl_len = (len > ctrl_offset) ? (len - ctrl_offset) : 0;
    if (ctrl_len > 0) {
      Serial.print("  [Control Payload] ");
      Serial.print(ctrl_len);
      Serial.println(" bytes — raw dump:");
      hexDump(buf + ctrl_offset, ctrl_len);
    } else {
      Serial.println("  [Control Payload] None.");
    }
  }

  Serial.println(
      "======================================================================");
}

// ================= INTERRUPTS =================
void setRxFlag() { receivedFlag = true; }

// ================= SETUP =================
void setup() {
  initBoard(); // Activates RADIO_POW_PIN, SPI bus, and e-paper display
  Serial.begin(115200);
  delay(1500);

  Serial.println("--------------------------------------------------");
  Serial.println("LORAMESHER PROMISCUOUS SNIFFER ONLINE");
  Serial.print("Monitoring Frequency : ");
  Serial.print(LoRa_frequency);
  Serial.println(" MHz");
  Serial.print("Spreading Factor     : SF");
  Serial.println(SpreadingFactor);
  Serial.print("Bandwidth            : ");
  Serial.print(Bandwidth);
  Serial.println(" kHz");
  Serial.print("Coding Rate          : 4/");
  Serial.println(CodeRate);
  Serial.println(
      "Sync Word            : 0x14 (20 — LoRaMesher private network)");
  Serial.println("--------------------------------------------------");

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio initialization failed, code: ");
    Serial.println(state);
    while (true)
      ;
  }

  // Match LoRaMesher testbed radio parameters exactly
  radio.setFrequency(LoRa_frequency);
  radio.setBandwidth(Bandwidth);
  radio.setSpreadingFactor(SpreadingFactor);
  radio.setCodingRate(CodeRate);
  radio.setOutputPower(OutputPower);

  // CRITICAL: must match LORA_SYNC_WORD = 20 (0x14) set in the testbed
  // firmware. The SX1262 hardware filters out every packet whose sync word
  // doesn't match — without this line the sniffer will receive nothing from the
  // testbed.
  radio.setSyncWord(20);

  radio.setDio1Action(setRxFlag);
  radio.startReceive();

  Serial.println("Sniffer active — listening for LoRaMesher frames...");
}

// ================= LOOP =================
void loop() {
  if (receivedFlag) {
    receivedFlag = false;

    // Read raw binary bytes — LoRaMesher frames are NOT null-terminated
    // strings. Use the uint8_t* overload of readData() to get the actual binary
    // content.
    uint8_t rxBuf[256];
    int state = radio.readData(rxBuf, sizeof(rxBuf));

    if (state == RADIOLIB_ERR_NONE) {
      size_t rxLen = radio.getPacketLength();
      decodeLoRaMesherPacket(rxBuf, rxLen);
    } else {
      Serial.print("Rx Error, RadioLib code: ");
      Serial.println(state);
    }

    // Return radio to continuous receive immediately
    radio.startReceive();
  }
}
