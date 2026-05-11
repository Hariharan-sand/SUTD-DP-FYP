#include <RadioLib.h>
#include "boards.h"

// CHANGE THIS PER BOARD
#define NODE_ID "003" 
#define DESTINATION_ID "001"

SX1262 radio = new Module(
  RADIO_CS_PIN,
  RADIO_DIO1_PIN,
  RADIO_RST_PIN,
  RADIO_BUSY_PIN
);

// ================= 1. DEFINE STRUCT FIRST =================
struct Packet { 
  String recipient; 
  String route; 
  String payload; 
};

// ================= FLAGS & STATE =================
volatile bool receivedFlag = false;
volatile bool transmittedFlag = false;

String outgoingPacket = "";
bool readyToSend = false;

unsigned long rttStart = 0;
bool rttActive = false;
unsigned long lastPingTime = 0;
const unsigned long pingInterval = 3000; // 3 seconds

int packetsSent = 0;
int packetsReceived = 0;
String lastRttStr = "---";

// ================= INTERRUPTS =================
void setRxFlag() { receivedFlag = true; }
void setTxFlag() { transmittedFlag = true; }

// ================= YOUR CUSTOM DISPLAY CODE =================
void updateDisplay(String status, String rxMsg, String txMsg) {
  display.setRotation(3);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  display.setCursor(0, 15);
  display.println("Node: " + String(NODE_ID));

  display.setCursor(0, 35);
  display.println("Status: " + status);

  display.setCursor(0, 55);
  display.println("RX:");
  display.setCursor(40, 55);
  display.println(rxMsg);

  display.setCursor(0, 75);
  display.println("TX:");
  display.setCursor(40, 75);
  display.println(txMsg);

  display.update();
}

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

// ================= SETUP =================
void setup() {
  initBoard(); 
  Serial.begin(115200);
  
  // Initial screen show
  updateDisplay("BOOTING", "none", "none");

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("Radio fail");
    while (true);
  }

  // Fast Settings (SF5, 500kHz)
  radio.setFrequency(LoRa_frequency);
  radio.setBandwidth(Bandwidth);
  radio.setSpreadingFactor(SpreadingFactor);
  radio.setCodingRate(CodeRate);
  radio.setOutputPower(OutputPower);

  radio.setDio1Action(setRxFlag);
  radio.startReceive();
  
  updateDisplay("IDLE", "ready", "ready");
  Serial.println("System Ready. Node: " + String(NODE_ID));
}

// ================= LOOP =================
void loop() {
  unsigned long currentTime = millis();

  // --- AUTO PING (MASTER NODE 003 ONLY) ---
  if (String(NODE_ID) == "003" && currentTime - lastPingTime >= pingInterval) {
    lastPingTime = currentTime;
    
    String payload = "ping_" + String(packetsSent);
    outgoingPacket = String(DESTINATION_ID) + "|" + String(NODE_ID) + "|" + payload;
    
    rttStart = millis();
    rttActive = true;
    readyToSend = true;
    packetsSent++;
    
    Serial.println("TX Auto-Ping...");
    // Only update display occasionally or on serial TX to prevent lag
  }

  // --- RX HANDLER ---
  if (receivedFlag) {
    receivedFlag = false;
    String str;
    int state = radio.readData(str);

    if (state == RADIOLIB_ERR_NONE) {
      Packet pkt = parsePacket(str);
      
      if (pkt.recipient == NODE_ID) {
        // Master Node Logic: Received our bounce back
        if (String(NODE_ID) == "003" && rttActive && pkt.payload.startsWith("ping_")) {
          long rtt = millis() - rttStart;
          rttActive = false;
          packetsReceived++;
          lastRttStr = String(rtt) + "ms";
          
          Serial.print("RTT Success: "); Serial.println(lastRttStr);
        //   updateDisplay("RTT SUCCESS", pkt.payload, lastRttStr);
        } 
        // Reflector Node Logic: Send it back immediately
        else if (String(NODE_ID) != "003") {
          outgoingPacket = pkt.route + "||" + pkt.payload;
          readyToSend = true;
          
          Serial.println("Reflecting packet...");
        //   updateDisplay("REFLECTING", pkt.payload, outgoingPacket);
        }
      }
    }
    radio.startReceive();
  }

  // --- TX HANDLER ---
  if (readyToSend) {
    readyToSend = false;
    delay(50); 
    radio.setDio1Action(setTxFlag);
    radio.startTransmit(outgoingPacket);
  }

  if (transmittedFlag) {
    transmittedFlag = false;
    radio.setDio1Action(setRxFlag);
    radio.startReceive();
  }
}