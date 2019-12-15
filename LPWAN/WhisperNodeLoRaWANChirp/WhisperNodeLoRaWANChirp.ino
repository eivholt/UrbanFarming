// Talk2 Whisper Node LoRa in LoRaWAN configuration for use with The Things Network
// https://bitbucket.org/talk2/whisper-node-avr-lora/src/master/
// https://bitbucket.org/talk2/arduino-ide-boards/src/master/
// JP15 jumped
// I2C Soil moisture sensor "Chirp!"
// https://github.com/Miceuz/i2c-moisture-sensor
// https://github.com/Apollon77/I2CSoilMoistureSensor
// Connection:

#include <T2WhisperNode.h>
#include <LowPower.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <RH_RF95.h>

#define DEBUG

const int nssPin = 10;
const int rstPin = 7;
const int dioPin = 2;

#define RADIO_TX_POWER 5

RH_RF95 myRadio;
T2Flash myFlash;

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
static const u1_t PROGMEM APPEUI[8]={ 0x26, 0xBD, 0x00, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]={ 0x03, 0x4B, 0xFB, 0x7C, 0xBA, 0xA6, 0xA3, 0x00 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t PROGMEM APPKEY[16] = { 0x21, 0x69, 0xEC, 0xAC, 0x8C, 0x29, 0xDC, 0x38, 0x37, 0xC3, 0xAE, 0x64, 0xC4, 0x9D, 0x3D, 0x8E };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}

static byte mydata[6]; 
static int counter;
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 15;

// Pin mapping
const lmic_pinmap lmic_pins = {
  .nss = nssPin,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = rstPin,
  .dio = {dioPin, A2, LMIC_UNUSED_PIN},
};

#include <I2CSoilMoistureSensor.h>
#include <Wire.h>
I2CSoilMoistureSensor sensor;

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
      #ifdef DEBUG
        Serial.println(F("OP_TXRXPEND, not sending"));
        #endif
    } else {
        // Prepare upstream data transmission at the next possible time.
        #ifdef DEBUG
        Serial.println(F("do_send"));
        #endif
        
        uint16_t capacitance = sensor.getCapacitance();
        int16_t tempc1 = sensor.getTemperature();
        uint16_t voltage = GetVoltage();
        mydata[0] = capacitance >> 8;
        mydata[1] = capacitance;
        mydata[2] = tempc1 >> 8;
        mydata[3] = tempc1;
        mydata[4] = voltage >> 8;
        mydata[5] = voltage;
        #ifdef DEBUG
        Serial.print(F("Capacitance: "));
        Serial.println(sensor.getCapacitance());
        Serial.print(F("Temperature: "));
        Serial.println(sensor.getTemperature());
        Serial.print(F("Voltage: "));
        Serial.println((int)voltage);
        #endif

        //myRadio.setTxPower(RADIO_TX_POWER);
        myRadio.printRegisters();
        myRadio.setSignalBandwidth(7.8E3);
        LMIC_setTxData2(1, mydata, sizeof(mydata), 0);
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
  delay(1000);
  // Setup the Blue LED pin
  pinMode(T2_WPN_LED_1, OUTPUT);   // Set LED pint to OUTPUT
  digitalWrite(T2_WPN_LED_1, LOW);
  // Setup the Yellow LED pin
  pinMode(T2_WPN_LED_2, OUTPUT);   // Set LED pint to OUTPUT
  digitalWrite(T2_WPN_LED_2, LOW);

  DisableNonEssentials();

  Serial.begin(115200);

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  //LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100); // needed for lora stack to work on feather

  // Make LMiC initialize the default channels, choose a channel, and
  // schedule the OTAA join
  digitalWrite(T2_WPN_LED_1, HIGH);
  LMIC_startJoining();

  Wire.begin();
  sensor.begin(true); // reset sensor
  Serial.print(F("I2C Soil Moisture Sensor Address: "));
  Serial.println(sensor.getAddress(),HEX);
  Serial.print(F("Sensor Firmware version: "));
  Serial.println(sensor.getVersion(),HEX);
  Serial.println();

  // Start job (sending automatically starts OTAA too)
  do_send(&sendjob);
}

void loop() {
  // Serial.print(F("I2C Soil Moisture Sensor Address: "));
  // Serial.println(sensor.getAddress(),HEX);
  // Serial.print(F("Sensor Firmware version: "));
  // Serial.println(sensor.getVersion(),HEX);
  // Serial.println();
  os_runloop_once();
}

void PowerDown()
{
  #ifdef DEBUG
  Serial.println(F("PowerDown"));
  Serial.flush();
  #endif
  digitalWrite(T2_WPN_LED_2, HIGH);
  myRadio.sleep();  
  //attachInterrupt(digitalPinToInterrupt(wakeUpPin), wakeUp, HIGH);
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
}

void PowerUp()
{
  //detachInterrupt(digitalPinToInterrupt(wakeUpPin));
  #ifdef DEBUG
  Serial.println(F("PowerUp"));
  Serial.flush();
  #endif
  digitalWrite(T2_WPN_LED_2, LOW);
}

void DisableNonEssentials()
{
  // Setup the Blue LED pin
  digitalWrite(T2_WPN_LED_1, LOW);
  pinMode(T2_WPN_LED_1, OUTPUT);   // Set LED pint to OUTPUT
  
  // Setup the Yellow LED pin
  digitalWrite(T2_WPN_LED_2, LOW);
  pinMode(T2_WPN_LED_2, OUTPUT);   // Set LED pint to OUTPUT
  
  myFlash.init(T2_WPN_FLASH_SPI_CS);
  myFlash.powerDown();
}

void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(F(": "));
    switch(ev) {
       case EV_SCAN_TIMEOUT:
           Serial.println(F("EV_SCAN_TIMEOUT"));
           break;
       case EV_BEACON_FOUND:
           Serial.println(F("EV_BEACON_FOUND"));
           break;
       case EV_BEACON_MISSED:
           Serial.println(F("EV_BEACON_MISSED"));
           break;
       case EV_BEACON_TRACKED:
           Serial.println(F("EV_BEACON_TRACKED"));
           break;
       case EV_JOINING:
           Serial.println(F("EV_JOINING"));
           break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            digitalWrite(T2_WPN_LED_1, LOW);
            // Ignore the channels from the Join Accept
            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            #ifdef DEBUG
            Serial.println(F("EV_JOINED"));
            Serial.flush();
            #endif
            break;
       case EV_RFU1:
           Serial.println(F("EV_RFU1"));
           break;
       case EV_JOIN_FAILED:
           Serial.println(F("EV_JOIN_FAILED"));
           break;
       case EV_REJOIN_FAILED:
           Serial.println(F("EV_REJOIN_FAILED"));
           break;
        case EV_TXCOMPLETE:
            #ifdef DEBUG
            Serial.println(F("EV_TXCOMPLETE"));
            Serial.flush();
            #endif
            //Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
//            if (LMIC.txrxFlags & TXRX_ACK)
//              Serial.println(F("Received ack"));
//            if (LMIC.dataLen) {
//              Serial.println(F("Received "));
//              Serial.println(LMIC.dataLen);
//              Serial.println(F(" bytes of payload"));
//            }
            PowerDown();
            PowerUp();
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
       case EV_LOST_TSYNC:
           Serial.println(F("EV_LOST_TSYNC"));
           break;
       case EV_RESET:
           Serial.println(F("EV_RESET"));
           break;
       case EV_RXCOMPLETE:
           // data received in ping slot
           Serial.println(F("EV_RXCOMPLETE"));
           break;
       case EV_LINK_DEAD:
           Serial.println(F("EV_LINK_DEAD"));
           break;
       case EV_LINK_ALIVE:
           Serial.println(F("EV_LINK_ALIVE"));
           break;
         default:
            #ifdef DEBUG
            Serial.print(F("Unknown event:"));
            Serial.println(ev);
            Serial.flush();
            #endif
            break;
    }
}

uint16_t GetVoltage()
{
  uint16_t voltage = 0;
  voltage = T2Utils::readVoltage(T2_WPN_VBAT_VOLTAGE, T2_WPN_VBAT_CONTROL);
  if (voltage == 0)
  {
    voltage = T2Utils::readVoltage(T2_WPN_VIN_VOLTAGE, T2_WPN_VIN_CONTROL);
  }
  return voltage;
}
