// =============================================================================
// NODE A  —  Ping-Pong Test (Initiator / Pinger)
// =============================================================================
// Behaviour:
//   * Every 10 seconds, transmits a PingPacket to Node B.
//   * Waits for Node B to echo back a PongPacket that carries:
//       - The original seq number
//       - Node B's receive RSSI (A->B leg) encoded as int16 * 10
//   * On reception, Node A prints:
//       - "PING-PONG SUCCESSFUL"
//       - Seq number
//       - Round-trip time (ms)
//       - A->B RSSI  (read by B and echoed back in the pong)
//       - B->A RSSI  (read by A from the pong reception)
//
// Wire format (binary, packed):
//   PingPacket  : magic(2B) | seq(4B) | tx_ms(4B)             = 10 bytes
//   PongPacket  : magic(2B) | seq(4B) | b_rssi_x10(2B signed) = 8 bytes
//
//   magic 0xAB01 -> Ping
//   magic 0xAB02 -> Pong
// =============================================================================

#include <RadioLib.h>
#include "boards.h"

// ---------- Radio ----------
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

// ---------- Ping interval ----------
static const unsigned long PING_INTERVAL_MS = 10000UL; // 10 seconds

// ---------- Wire-format structs ----------
#pragma pack(push, 1)
struct PingPacket {
    uint16_t magic;       // 0xAB01
    uint32_t seq;         // monotonically increasing sequence number
    uint32_t tx_ms;       // millis() at transmission time on Node A
};

struct PongPacket {
    uint16_t magic;       // 0xAB02
    uint32_t seq;         // echoed back from the ping
    int16_t  b_rssi_x10;  // Node B receive RSSI x10 (preserves 1 decimal, avoids float on wire)
};
#pragma pack(pop)

static const uint16_t MAGIC_PING = 0xAB01;
static const uint16_t MAGIC_PONG = 0xAB02;

// ---------- State ----------
volatile bool rxFlag   = false;
volatile bool txFlag   = false;

uint32_t      pingSeq       = 0;
uint32_t      pingTxMs      = 0;
bool          waitingForPong = false;
unsigned long lastPingTime   = 0;

// ---------- ISRs ----------
void setRxFlag() { rxFlag = true; }
void setTxFlag() { txFlag = true; }

// ---------- Helpers ----------
void radioStartReceive() {
    radio.setDio1Action(setRxFlag);
    radio.startReceive();
}

// Transmit a raw byte buffer, then switch back to RX
void transmitBytes(const uint8_t *buf, size_t len) {
    radio.setDio1Action(setTxFlag);
    radio.startTransmit(buf, len);
    // Spin briefly waiting for TX done (packet is tiny, < 1 ms airtime at SF5/BW500)
    unsigned long t = millis();
    while (!txFlag && millis() - t < 2000) { /* wait */ }
    txFlag = false;
    radioStartReceive();
}

// ---------- Setup ----------
void setup() {
    initBoard();
    Serial.begin(115200);
    delay(1500);

    Serial.println("==============================================");
    Serial.println("  NODE A  --  Ping-Pong Test (Initiator)");
    Serial.println("==============================================");

    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("Radio init failed, code: ");
        Serial.println(state);
        while (true);
    }

    radio.setFrequency(LoRa_frequency);
    radio.setBandwidth(Bandwidth);
    radio.setSpreadingFactor(SpreadingFactor);
    radio.setCodingRate(CodeRate);
    radio.setOutputPower(OutputPower);

    radioStartReceive();

    Serial.print("Frequency : "); Serial.print(LoRa_frequency); Serial.println(" MHz");
    Serial.print("SF        : "); Serial.println(SpreadingFactor);
    Serial.print("BW        : "); Serial.print(Bandwidth); Serial.println(" kHz");
    Serial.println("First ping in 10 seconds...");
}

// ---------- Loop ----------
void loop() {
    unsigned long now = millis();

    // ----------------------------------------------------------------
    // 1.  Send a ping every PING_INTERVAL_MS
    // ----------------------------------------------------------------
    if (now - lastPingTime >= PING_INTERVAL_MS) {
        lastPingTime = now;

        PingPacket ping;
        ping.magic = MAGIC_PING;
        ping.seq   = pingSeq++;
        ping.tx_ms = millis();

        pingTxMs       = ping.tx_ms;
        waitingForPong = true;

        Serial.print("\n[PING] Seq=");
        Serial.print(ping.seq);
        Serial.println("  Transmitting...");

        transmitBytes((const uint8_t *)&ping, sizeof(ping));
    }

    // ----------------------------------------------------------------
    // 2.  RX handler -- look for a PongPacket from Node B
    // ----------------------------------------------------------------
    if (rxFlag) {
        rxFlag = false;

        uint8_t buf[64];
        int state = radio.readData(buf, sizeof(buf));

        if (state == RADIOLIB_ERR_NONE) {
            size_t rxLen = radio.getPacketLength();

            if (rxLen >= sizeof(PongPacket)) {
                PongPacket pong;
                memcpy(&pong, buf, sizeof(PongPacket));

                if (pong.magic == MAGIC_PONG && waitingForPong) {
                    waitingForPong = false;

                    unsigned long rtt       = millis() - pingTxMs;
                    float         bToA_rssi = radio.getRSSI();                       // B->A leg
                    float         aToB_rssi = (float)pong.b_rssi_x10 / 10.0f;       // A->B leg (echoed by B)

                    Serial.println();
                    Serial.println("+-----------------------------------------+");
                    Serial.println("|       PING-PONG SUCCESSFUL!             |");
                    Serial.println("+-----------------------------------------+");
                    Serial.print(  "|  Seq #        : "); Serial.println(pong.seq);
                    Serial.print(  "|  Round-Trip   : "); Serial.print(rtt);         Serial.println(" ms");
                    Serial.print(  "|  A->B RSSI    : "); Serial.print(aToB_rssi, 1); Serial.println(" dBm  (read by B)");
                    Serial.print(  "|  B->A RSSI    : "); Serial.print(bToA_rssi, 1); Serial.println(" dBm  (read by A)");
                    Serial.println("+-----------------------------------------+");
                }
                // else: unexpected pong or not waiting -- ignore
            }
        } else {
            Serial.print("[RX Error] code: ");
            Serial.println(state);
        }

        // Re-arm the receiver
        radioStartReceive();
    }
}
