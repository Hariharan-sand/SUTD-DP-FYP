// =============================================================================
// NODE B  --  Ping-Pong Test (Reflector / Pong)
// =============================================================================
// Behaviour:
//   * Listens continuously for a PingPacket from Node A.
//   * On receipt, immediately echoes back a PongPacket containing:
//       - The original seq number (so A can match it)
//       - Node B's receive RSSI  (A->B leg) encoded as int16 * 10
//   * Serial-prints each ping reception with its RSSI and SNR.
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

// ---------- Wire-format structs ----------
#pragma pack(push, 1)
struct PingPacket {
    uint16_t magic;       // 0xAB01
    uint32_t seq;         // sequence number from Node A
    uint32_t tx_ms;       // transmit timestamp from Node A (unused by B, ignored)
};

struct PongPacket {
    uint16_t magic;       // 0xAB02
    uint32_t seq;         // echoed sequence number
    int16_t  b_rssi_x10;  // B's receive RSSI * 10  (e.g. -875 means -87.5 dBm)
};
#pragma pack(pop)

static const uint16_t MAGIC_PING = 0xAB01;
static const uint16_t MAGIC_PONG = 0xAB02;

// ---------- State ----------
volatile bool rxFlag = false;
volatile bool txFlag = false;

uint32_t pongsReflected = 0;

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
    // Spin waiting for TX done -- packet is tiny, done in < 1 ms at SF5/BW500
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
    Serial.println("  NODE B  --  Ping-Pong Test (Reflector)");
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
    Serial.println("Listening for pings from Node A...");
}

// ---------- Loop ----------
void loop() {

    if (rxFlag) {
        rxFlag = false;

        uint8_t buf[64];
        int state = radio.readData(buf, sizeof(buf));

        if (state == RADIOLIB_ERR_NONE) {
            size_t rxLen = radio.getPacketLength();

            // Capture radio metrics BEFORE we call transmitBytes (which changes the radio state)
            float rxRssi = radio.getRSSI();
            float rxSnr  = radio.getSNR();

            if (rxLen >= sizeof(PingPacket)) {
                PingPacket ping;
                memcpy(&ping, buf, sizeof(PingPacket));

                if (ping.magic == MAGIC_PING) {
                    Serial.print("\n[PING RX] Seq=");
                    Serial.print(ping.seq);
                    Serial.print("  RSSI=");
                    Serial.print(rxRssi, 1);
                    Serial.print(" dBm  SNR=");
                    Serial.print(rxSnr, 1);
                    Serial.println(" dB  -> Reflecting pong...");

                    // Build and transmit the PongPacket
                    PongPacket pong;
                    pong.magic      = MAGIC_PONG;
                    pong.seq        = ping.seq;
                    pong.b_rssi_x10 = (int16_t)(rxRssi * 10.0f);  // encode RSSI * 10

                    transmitBytes((const uint8_t *)&pong, sizeof(pong));

                    pongsReflected++;
                    Serial.print("[PONG TX] Sent. Total reflected: ");
                    Serial.println(pongsReflected);
                }
                // else: not a ping, ignore
            }
        } else {
            Serial.print("[RX Error] code: ");
            Serial.println(state);
            radioStartReceive();
        }
    }
}
