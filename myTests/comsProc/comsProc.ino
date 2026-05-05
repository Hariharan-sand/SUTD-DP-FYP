#include <RadioLib.h>
#include "boards.h"

#define NODE_ID "003"  //node ID for this device


SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
volatile bool receivedFlag = false;
volatile bool transmittedFlag = false;

String outgoingPacket = "";
bool readyToSend = false;

// Display tracking
String lastRX = "-";
String lastTX = "-";
String currentStatus = "INIT";

struct Packet {
  String recipient;
  String route;
  String payload;
};

void setRxFlag() {
  receivedFlag = true;
}
void setTxFlag() {
  transmittedFlag = true;
}

// display stuff
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

// the parser expects packets in the format: recipient|route|payload
Packet parsePacket(String msg) {
  Packet p;

  int first = msg.indexOf('|');
  int second = msg.indexOf('|', first + 1);

  if (first == -1 || second == -1) {
    Serial.println("Malformed packet!");
    return p;
  }

  p.recipient = msg.substring(0, first);
  p.route     = msg.substring(first + 1, second);
  p.payload   = msg.substring(second + 1);

  return p;
}

// packet handler
void handlePacket(Packet pkt) {

  // ignore if id is not mine
  if (pkt.recipient != NODE_ID) {
    Serial.println("msg not for me");
    return;
  }

  Serial.println("Packet is for me");

  // show received message
  lastRX = pkt.recipient + "|" + pkt.route + "|" + pkt.payload;
  currentStatus = "RX";
  updateDisplay(currentStatus, lastRX, lastTX);

  // check if the message ends here
  if (pkt.route.length() == 0) {
    Serial.println("FINAL MESSAGE: " + pkt.payload);

    currentStatus = "FINAL";
    lastRX = pkt.payload;
    updateDisplay(currentStatus, lastRX, lastTX);
    return;
  }

  // format the next packet
  int comma = pkt.route.indexOf(',');

  String nextHop;
  if (comma == -1) {
    nextHop = pkt.route;
    pkt.route = "";
  } else {
    nextHop = pkt.route.substring(0, comma);
    pkt.route = pkt.route.substring(comma + 1);
  }

  String newPacket = nextHop + "|" + pkt.route + "|" + pkt.payload;

  Serial.println("Forwarding: " + newPacket);

  outgoingPacket = newPacket;
  readyToSend = true;

  lastTX = newPacket;
  currentStatus = "FWD";
  updateDisplay(currentStatus, lastRX, lastTX);
}

// handle serial input for testing. Format: route|payload
void handleSerialInput() {

  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input.length() == 0) return;

  Serial.println("Input: " + input);

  int sep = input.indexOf('|');

  if (sep == -1) {
    Serial.println("Invalid format! Use: 002,003|Hello");
    return;
  }

  String route = input.substring(0, sep);
  String payload = input.substring(sep + 1);

  // Extract first hop
  int comma = route.indexOf(',');

  String firstHop;
  if (comma == -1) {
    firstHop = route;
    route = "";
  } else {
    firstHop = route.substring(0, comma);
    route = route.substring(comma + 1);
  }

  String packet = firstHop + "|" + route + "|" + payload;

  Serial.println("Generated packet: " + packet);

  outgoingPacket = packet;
  readyToSend = true;
}


void setup() {
  initBoard();
  Serial.begin(115200);

  delay(1500);

  Serial.println("[SX1262] Initializing...");
  int state = radio.begin();

  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("Init failed!");
    while (true);
  }

  // LoRa config
  radio.setFrequency(LoRa_frequency);
  radio.setBandwidth(Bandwidth);
  radio.setSpreadingFactor(SpreadingFactor);
  radio.setCodingRate(CodeRate);
  radio.setOutputPower(OutputPower);

  // Start RX
  radio.setDio1Action(setRxFlag);
  radio.startReceive();

  currentStatus = "LISTEN";
  updateDisplay(currentStatus, lastRX, lastTX);

  Serial.println("Listening...");

}


void loop() {

    handleSerialInput();
  // receive 
  if (receivedFlag) {
    receivedFlag = false;

    String str;
    int state = radio.readData(str);

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("RX: " + str);

      Packet pkt = parsePacket(str);
      handlePacket(pkt);

    } else {
      Serial.println("RX error");
    }

    radio.startReceive();
  }

  // transmit
  if (readyToSend) {
    readyToSend = false;

    Serial.println("TX: " + outgoingPacket);

    lastTX = outgoingPacket;
    currentStatus = "TX";
    updateDisplay(currentStatus, lastRX, lastTX);

    radio.setDio1Action(setTxFlag);
    radio.startTransmit(outgoingPacket);
  }
  if (transmittedFlag) {
    transmittedFlag = false;

    Serial.println("TX done");

    currentStatus = "LISTEN";
    updateDisplay(currentStatus, lastRX, lastTX);

    radio.setDio1Action(setRxFlag);
    radio.startReceive();
  }
}