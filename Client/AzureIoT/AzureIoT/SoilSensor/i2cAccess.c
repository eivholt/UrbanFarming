#include "i2cAccess.h"
#include <applibs\i2c.h>

void WriteI2CRegister8bit(I2C_DeviceAddress sensorAddress, const uint8_t* value) {
	I2CMaster_Write(i2cFd, sensorAddress, value, sizeof(value));
}

uint8_t ReadI2CRegister8bit(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddress) {
	uint8_t returnValue;
	//ssize_t transferredBytes = 
		I2CMaster_WriteThenRead(i2cFd, sensorAddress, registerAddress, 1, &returnValue, 1);
	return returnValue;
}

uint16_t ReadI2CRegister16bitUnsigned(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddress)
{
	uint8_t valueFromRead[2];
	uint16_t returnValue;

	//ssize_t transferredBytes = 
		I2CMaster_WriteThenRead(i2cFd, sensorAddress, registerAddress, 1, valueFromRead, 2);
	returnValue = (uint16_t)(valueFromRead[0] << 8 | valueFromRead[1]);
	return returnValue;
}

uint16_t ReadI2CRegister16bitSigned(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddress)
{
	return (uint16_t)ReadI2CRegister16bitUnsigned(sensorAddress, registerAddress);
}