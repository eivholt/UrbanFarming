#pragma once
#include <applibs\i2c.h>

extern int i2cFd;

void WriteI2CRegister8bit(I2C_DeviceAddress sensorAddress, const uint8_t* value);

uint8_t ReadI2CRegister8bit(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddress);

uint16_t ReadI2CRegister16bitUnsigned(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddress);

uint16_t ReadI2CRegister16bitSigned(I2C_DeviceAddress addr, const uint8_t* registerAddress);
