#include <stdio.h>
#include "azure_iot_utilities.h"
#include <string.h>


/// <summary>
///     Log a message related to the Azure IoT Hub client with
///     prefix [Azure IoT Hub client]:"
/// </summary>
/// <param name="message">The format string containing the error to output along with
/// placeholders</param>
/// <param name="...">The list of arguments to populate the format string placeholders</param>
void LogMessage(char* message, ...)
{
	va_list args;
	va_start(args, message);
	Log_Debug("[Azure IoT Hub client] ");
	Log_DebugVarArgs(message, args);
	va_end(args);
}



/// <summary>
///     Function invoked whenever a Direct Method call is received from the IoT Hub.
/// </summary>
static DirectMethodCallFnType directMethodCallCb = 0;

static int directMethodCallback(const char* methodName, const unsigned char* payload, size_t size,
	unsigned char** response, size_t* response_size,
	void* userContextCallback);

bool AzureIoT_SetupClient(IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle)
{
	IoTHubDeviceClient_LL_SetDeviceMethodCallback(iothubClientHandle, directMethodCallback, NULL);
	return true;
}

/// <summary>
///     Sets the function to be invoked whenever a Direct Method call from the IoT Hub is received.
/// </summary>
/// <param name="callback">The callback function invoked when a Direct Method call is
/// received</param>
void AzureIoT_SetDirectMethodCallback(DirectMethodCallFnType callback)
{
	directMethodCallCb = callback;
}

/// <summary>
///     Callback when direct method is called.
/// </summary>
static int directMethodCallback(const char* methodName, const unsigned char* payload, size_t size,
	unsigned char** response, size_t* responseSize,
	void* userContextCallback)
{
	LogMessage("INFO: Trying to invoke method %s\n", methodName);

	int result = 404;

	if (directMethodCallCb != NULL) {
		char* responseFromCallback = NULL;
		size_t responseFromCallbackSize = 0;

		result = directMethodCallCb(methodName, payload, size, &responseFromCallback,
			&responseFromCallbackSize);
		*responseSize = responseFromCallbackSize;
		*response = responseFromCallback;
	}
	else {
		LogMessage("INFO: No method '%s' found, HttpStatus=%d\n", methodName, result);
		static const char methodNotFound[] = "\"No method found\"";
		*responseSize = strlen(methodNotFound);
		*response = (unsigned char*)malloc(*responseSize);
		if (*response != NULL) {
			strncpy((char*)(*response), methodNotFound, *responseSize);
		}
		else {
			LogMessage("ERROR: Cannot create response message for method call.\n");
			abort();
		}
	}

	return result;
}