#include "i2cAccess.h"

void WriteI2CRegister8bit(I2C_DeviceAddress sensorAddress, const uint8_t* registerAddressAndValue) {
	uint8_t buff[1];
	buff[0] = registerAddressAndValue[0];

	ssize_t transferredBytes =
		I2CMaster_Write(i2cFd, sensorAddress, buff, sizeof(buff));
	if (transferredBytes == -1)
		Log_Debug("ERROR: I2CMaster_Writer: errno=%d (%s)\n", errno, strerror(errno));

	buff[0] = registerAddressAndValue[1];
	transferredBytes =
		I2CMaster_Write(i2cFd, sensorAddress, buff, sizeof(buff));
	if (transferredBytes == -1)
		Log_Debug("ERROR: I2CMaster_Writer: errno=%d (%s)\n", errno, strerror(errno));
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