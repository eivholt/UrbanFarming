#pragma once
#include <stdarg.h>
#include <stdlib.h>
#include <applibs/log.h>
#include <stdbool.h>
#include <iothub_device_client_ll.h>

bool AzureIoT_SetupClient(IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle);

/// <summary>
///     Type of the function callback invoked when a Direct Method call from the IoT Hub is
///     received.
/// </summary>
/// <param name="directMethodName">The name of the direct method to invoke</param>
/// <param name="payload">The payload of the direct method call</param>
/// <param name="payloadSize">The payload size of the direct method call</param>
/// <param name="responsePayload">The payload of the response provided by the callee. It must be
/// either NULL or an heap allocated string.</param>
/// <param name="responsePayloadSize">The size of the response payload provided by the
/// callee.</param>
/// <returns>The HTTP status code. e.g. 404 for method not found.</returns>
typedef int (*DirectMethodCallFnType)(const char* directMethodName, const char* payload,
	size_t payloadSize, char** responsePayload,
	size_t* responsePayloadSize);

/// <summary>
///     Sets the function to be invoked whenever a Direct Method call from the IoT Hub is received.
/// </summary>
/// <param name="callback">The callback function invoked when a Direct Method call is
/// received</param>
void AzureIoT_SetDirectMethodCallback(DirectMethodCallFnType callback);