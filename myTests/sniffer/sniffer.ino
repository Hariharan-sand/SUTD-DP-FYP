#include <RadioLib.h>
#include "boards.h"

// Set as a unique ID, though a sniffer ignores recipient filters
#define NODE_ID "SNIFFER" 

SX1262 radio = new Module(
  RADIO_CS_PIN,
  RADIO_DIO1_PIN,
  RADIO_RST_PIN,
  RADIO_BUSY_PIN
);

// ================= FLAGS =================
volatile bool receivedFlag = false;

// ================= PACKET STRUCT =================
struct Packet {
  String recipient;
  String route;
  String payload;
  bool isValidFormat;
};

// ================= INTERRUPTS =================
void setRxFlag() { receivedFlag = true; }

// ================= PARSER =================
Packet parsePacket(String msg) {
  Packet p;
  p.isValidFormat = false;
  
  int a = msg.indexOf('|');
  int b = msg.indexOf('|', a + 1);

  if (a != -1 && b != -1) {
    p.recipient     = msg.substring(0, a);
    p.route         = msg.substring(a + 1, b);
    p.payload       = msg.substring(b + 1);
    p.isValidFormat = true;
  }
  return p;
}

// ================= SNIFFER MONITOR HANDLER =================
void sniffPacket(String rawMsg) {
  unsigned long timestamp = millis();
  Packet pkt = parsePacket(rawMsg);

  Serial.println("\n=================== PACKET INTERCEPTED ===================");
  Serial.print("Timestamp    : "); Serial.print(timestamp); Serial.println(" ms");
  Serial.print("Raw String   : "); Serial.println(rawMsg);
  
  // Print signal diagnostics if available
  Serial.print("RSSI         : "); Serial.print(radio.getRSSI()); Serial.println(" dBm");
  Serial.print("SNR          : "); Serial.print(radio.getSNR()); Serial.println(" dB");
  Serial.println("----------------------------------------------------------");

  if (pkt.isValidFormat) {
    Serial.print("Intended Recipient : "); Serial.println(pkt.recipient);
    Serial.print("Remaining Route    : "); Serial.println(pkt.route.length() > 0 ? pkt.route : "[None - Direct Hop]");
    Serial.print("Extracted Payload  : "); Serial.println(pkt.payload);
  } else {
    Serial.println("Warning: Intercepted packet does not match standard Net-Protocol format.");
  }
  Serial.println("==========================================================");
}

// ================= SETUP =================
void setup() {
  initBoard(); // Ensures Radio power rail (RADIO_POW_PIN) and SPI pins activate
  Serial.begin(115200);
  delay(1500);

  Serial.println("--------------------------------------------------");
  Serial.print("PROMISCUOUS SNIFFER NODE ONLINE: ");
  Serial.println(NODE_ID);
  Serial.print("Monitoring Frequency: "); Serial.print(LoRa_frequency); Serial.println(" MHz");
  Serial.println("--------------------------------------------------");

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio initialization failed, code: ");
    Serial.println(state);
    while (true);
  }

  // Radio Parameters matching your custom network setup
  radio.setFrequency(LoRa_frequency);
  radio.setBandwidth(Bandwidth);
  radio.setSpreadingFactor(SpreadingFactor);
  radio.setCodingRate(CodeRate);
  radio.setOutputPower(OutputPower);

  // Set up the interrupt action specifically to intercept payloads
  radio.setDio1Action(setRxFlag);
  radio.startReceive();
}

// ================= LOOP =================
void loop() {
  // --- RECEIVE EVENT ---
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);

    if (state == RADIOLIB_ERR_NONE) {
      // Pass the transmission straight to our printing engine
      sniffPacket(str);
    } else {
      Serial.print("Rx Error encountered, RadioLib Code: ");
      Serial.println(state);
    }
    
    // Put the radio right back into listening mode immediately
    radio.startReceive();
  }
}