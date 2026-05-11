#include <RadioLib.h>
#include "boards.h"

// CHANGE THIS FOR EACH BOARD (e.g., "001", "003")
#define NODE_ID "001" 

SX1262 radio = new Module(
  RADIO_CS_PIN,
  RADIO_DIO1_PIN,
  RADIO_RST_PIN,
  RADIO_BUSY_PIN
);

// ================= FLAGS =================
volatile bool receivedFlag = false;
volatile bool transmittedFlag = false;

// ================= STATE =================
String outgoingPacket = "";
bool readyToSend = false;

// ================= RTT =================
unsigned long rttStart = 0;
bool rttActive = false;
String lastSentPayload = "";

// ================= PACKET STRUCT =================
struct Packet {
  String recipient;
  String route;
  String payload;
};

// ================= INTERRUPTS =================
void setRxFlag() { receivedFlag = true; }
void setTxFlag() { transmittedFlag = true; }

// ================= PARSER =================
Packet parsePacket(String msg) {
  Packet p;
  int a = msg.indexOf('|');
  int b = msg.indexOf('|', a + 1);

  if (a != -1 && b != -1) {
    p.recipient = msg.substring(0, a);
    p.route     = msg.substring(a + 1, b);
    p.payload   = msg.substring(b + 1);
  }
  return p;
}

// ================= SERIAL INPUT =================
// Input format: 001,003|speedtest
void handleSerial() {
  if (!Serial.available()) return;

  String in = Serial.readStringUntil('\n');
  in.trim();

  int sep = in.indexOf('|');
  if (sep == -1) return;

  String route = in.substring(0, sep);
  String payload = in.substring(sep + 1);

  // Start RTT timer and save payload to verify on return
  rttStart = millis();
  rttActive = true;
  lastSentPayload = payload;
  
  Serial.println("--- TIMER STARTED ---");

  int comma = route.indexOf(',');
  String firstHop;
  if (comma == -1) {
    firstHop = route;
    route = "";
  } else {
    firstHop = route.substring(0, comma);
    route = route.substring(comma + 1);
  }

  outgoingPacket = firstHop + "|" + route + "|" + payload;
  readyToSend = true;
}

// ================= HANDLER =================
void handlePacket(Packet pkt) {
  // If packet isn't addressed to this node, ignore it
  if (pkt.recipient != NODE_ID) return;

  // 1. Check if this is our own message coming back
  if (rttActive && pkt.payload == lastSentPayload) {
    unsigned long duration = millis() - rttStart;
    Serial.println("***************************");
    Serial.print("ROUND TRIP COMPLETE: ");
    Serial.print(duration);
    Serial.println(" ms");
    Serial.println("***************************");
    rttActive = false;
    return; // Don't re-process or forward our own returned message
  }

  Serial.println("RX: " + pkt.payload);

  // 2. Forwarding Logic (If there are more hops in the route)
  if (pkt.route.length() > 0) {
    int comma = pkt.route.indexOf(',');
    String nextHop;
    String remainingRoute;

    if (comma == -1) {
      nextHop = pkt.route;
      remainingRoute = "";
    } else {
      nextHop = pkt.route.substring(0, comma);
      remainingRoute = pkt.route.substring(comma + 1);
    }

    outgoingPacket = nextHop + "|" + remainingRoute + "|" + pkt.payload;
    readyToSend = true;
    Serial.println("Forwarding to: " + nextHop);
  } else {
    Serial.println("Final destination reached. No further hops.");
  }
}

// ================= SETUP =================
void setup() {
  initBoard(); // Still needed for pins/power, but no e-paper calls
  Serial.begin(115200);
  delay(1500);

  Serial.println("-------------------------");
  Serial.print("NODE ONLINE: ");
  Serial.println(NODE_ID);
  Serial.println("-------------------------");

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("Radio fail");
    while (true);
  }

  // Radio Parameters
  radio.setFrequency(LoRa_frequency);
  radio.setBandwidth(Bandwidth);
  radio.setSpreadingFactor(SpreadingFactor);
  radio.setCodingRate(CodeRate);
  radio.setOutputPower(OutputPower);

  radio.setDio1Action(setRxFlag);
  radio.startReceive();
}

// ================= LOOP =================
void loop() {
  handleSerial();

  // --- RECEIVE EVENT ---
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);

    if (state == RADIOLIB_ERR_NONE) {
      Packet pkt = parsePacket(str);
      handlePacket(pkt);
    }
    radio.startReceive();
  }

  // --- TRANSMIT EVENT ---
  if (readyToSend) {
    readyToSend = false;
    
    // Tiny delay to ensure radio hardware is ready to switch from RX to TX
    delay(50); 
    
    radio.setDio1Action(setTxFlag);
    int state = radio.startTransmit(outgoingPacket);
    
    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("Sent: " + outgoingPacket);
    }
  }

  // --- TX FINISHED ---
  if (transmittedFlag) {
    transmittedFlag = false;
    radio.setDio1Action(setRxFlag);
    radio.startReceive();
  }
}