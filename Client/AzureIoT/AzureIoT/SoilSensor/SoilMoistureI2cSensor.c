#include "i2cAccess.h"
#include "SoilMoistureI2cSensor.h"
#include <unistd.h>

const uint8_t ctrlVersionData[] = { SOILMOISTURESENSOR_GET_VERSION, 0x00 };
const uint8_t ctrlGetAddressData[] = { SOILMOISTURESENSOR_GET_ADDRESS, 0x00 };
uint8_t ctrlSetAddressData[] = { SOILMOISTURESENSOR_SET_ADDRESS, 0x00 };
const uint8_t ctrlGetBusyData[] = { SOILMOISTURESENSOR_GET_BUSY, 0x00 };
const uint8_t ctrlTemperatureData[] = { SOILMOISTURESENSOR_GET_TEMPERATURE, 0x00 };
const uint8_t ctrlCapacitanceData[] = { SOILMOISTURESENSOR_GET_CAPACITANCE, 0x00 };
const uint8_t ctrlResetData[] = { SOILMOISTURESENSOR_RESET, 0x00 };

void ResetSoilSensor(I2C_DeviceAddress sensorAddress) {
	WriteI2CRegister8bit(sensorAddress, ctrlResetData);
}

void InitializeSoilSensor(I2C_DeviceAddress sensorAddress, bool waitForSensor) {
	ResetSoilSensor(sensorAddress);
	if (waitForSensor) {
		sleep(1);
	}
}

void SetAddress(I2C_DeviceAddress sensorAddress, I2C_DeviceAddress desiredAddress, bool reset) {
	ctrlSetAddressData[1] = (uint8_t)desiredAddress;
	// Unclear why this has to be done twice,
	// see https://github.com/Apollon77/I2CSoilMoistureSensor/blob/master/I2CSoilMoistureSensor.cpp
	WriteI2CRegister8bit(sensorAddress, ctrlSetAddressData);
	WriteI2CRegister8bit(sensorAddress, ctrlSetAddressData);
	if (reset) {
		ResetSoilSensor(sensorAddress);
	}
}

uint8_t GetVersion(I2C_DeviceAddress sensorAddress) {
	uint8_t soilSensorVersion = ReadI2CRegister8bit(sensorAddress, ctrlVersionData);
	Log_Debug("Soil sensor (Address: %X) firmware version: %X\n", sensorAddress, soilSensorVersion);
	return soilSensorVersion;
}

uint8_t GetAddress(I2C_DeviceAddress sensorAddress) {
	uint8_t soilSensorAddress = ReadI2CRegister8bit(sensorAddress, ctrlGetAddressData);
	Log_Debug("Soil sensor (Address: %X) i2c reporting address: %X\n", sensorAddress, soilSensorAddress);
	return soilSensorAddress;
}

bool IsBusy(I2C_DeviceAddress sensorAddress) {
	return (ReadI2CRegister8bit(sensorAddress, ctrlGetBusyData) == 1);
}

float GetTemperature(I2C_DeviceAddress sensorAddress) {
	return (float)ReadI2CRegister16bitSigned(sensorAddress, ctrlTemperatureData) / 10;
}

unsigned int GetCapacitance(I2C_DeviceAddress sensorAddress) {
	return ReadI2CRegister16bitUnsigned(sensorAddress, ctrlCapacitanceData);
}