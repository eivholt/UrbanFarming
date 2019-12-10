// Talk2 Whisper Node LoRa in LoRaWAN configuration for use with The Things Network
// https://bitbucket.org/talk2/whisper-node-avr-lora/src/master/
// JP15 & JP12 jumped
// I2C Soil moisture sensor "Chirp!"
// https://github.com/Miceuz/i2c-moisture-sensor
// https://github.com/Apollon77/I2CSoilMoistureSensor
// Connection:

#include <I2CSoilMoistureSensor.h>
#include <Wire.h>

#define debugSerial Serial

I2CSoilMoistureSensor sensor;

void setup() {
  Wire.begin();
  debugSerial.begin(9600);

  // Wait a maximum of 10s for Serial Monitor
  while (!debugSerial && millis() < 3000)
    ;

  sensor.begin(true); // reset sensor
  Serial.print("I2C Soil Moisture Sensor Address: ");
  Serial.println(sensor.getAddress(),HEX);
  Serial.print("Sensor Firmware version: ");
  Serial.println(sensor.getVersion(),HEX);
  Serial.println();
}

void loop() {
  Serial.print("Sensor Firmware version: ");
  Serial.println(sensor.getVersion());
  delay(1000);
}
