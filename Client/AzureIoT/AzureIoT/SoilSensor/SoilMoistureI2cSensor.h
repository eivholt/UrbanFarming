#pragma once
#include "i2cAccess.h"
#include <stdbool.h>
#include <applibs/log.h>

//Soil Moisture Sensor Register Addresses
#define SOILMOISTURESENSOR_GET_CAPACITANCE 	0x00 // (r) 	2 bytes
#define SOILMOISTURESENSOR_SET_ADDRESS 		0x01 //	(w) 	1 byte
#define SOILMOISTURESENSOR_GET_ADDRESS 		0x02 // (r) 	1 byte
#define SOILMOISTURESENSOR_MEASURE_LIGHT 	0x03 //	(w) 	n/a
#define SOILMOISTURESENSOR_GET_LIGHT 		0x04 //	(r) 	2 bytes
#define SOILMOISTURESENSOR_GET_TEMPERATURE	0x05 //	(r) 	2 bytes
#define SOILMOISTURESENSOR_RESET 			0x06 //	(w) 	n/a
#define SOILMOISTURESENSOR_GET_VERSION 		0x07 //	(r) 	1 bytes
#define SOILMOISTURESENSOR_SLEEP	        0x08 // (w)     n/a
#define SOILMOISTURESENSOR_GET_BUSY	        0x09 // (r)	    1 bytes

void ResetSoilSensor(I2C_DeviceAddress sensorAddress);

void InitializeSoilSensor(I2C_DeviceAddress sensorAddress, bool waitForSensor);

void SetAddress(I2C_DeviceAddress sensorAddress, I2C_DeviceAddress desiredAddress, bool reset);

uint8_t GetVersion(I2C_DeviceAddress sensorAddress);

uint8_t GetAddress(I2C_DeviceAddress sensorAddress);

bool IsBusy(I2C_DeviceAddress sensorAddress);

float GetTemperature(I2C_DeviceAddress sensorAddress);

unsigned int GetCapacitance(I2C_DeviceAddress sensorAddress);
