/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

// This sample C application for Azure Sphere demonstrates Azure IoT SDK C APIs
// The application uses the Azure IoT SDK C APIs to
// 1. Use the buttons to trigger sending telemetry to Azure IoT Hub/Central.
// 2. Use IoT Hub/Device Twin to control an LED.

// You will need to provide four pieces of information to use this application, all of which are set
// in the app_manifest.json.
// 1. The Scope Id for your IoT Central application (set in 'CmdArgs')
// 2. The Tenant Id obtained from 'azsphere tenant show-selected' (set in 'DeviceAuthentication')
// 3. The Azure DPS Global endpoint address 'global.azure-devices-provisioning.net'
//    (set in 'AllowedConnections')
// 4. The IoT Hub Endpoint address for your IoT Central application (set in 'AllowedConnections')

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/i2c.h>
#include <applibs/log.h>
#include <applibs/networking.h>
#include <applibs/gpio.h>
#include <applibs/storage.h>

// By default, this sample is targeted at the MT3620 Reference Development Board (RDB).
// This can be changed using the project property "Target Hardware Definition Directory".
// This #include imports the sample_hardware abstraction from that hardware definition.
#include <hw/sample_hardware.h>

#include "epoll_timerfd_utilities.h"

// Azure IoT SDK
#include <iothub_client_core_common.h>
#include <iothub_device_client_ll.h>
#include <iothub_client_options.h>
#include <iothubtransportmqtt.h>
#include <iothub.h>
#include <azure_sphere_provisioning.h>
#include "azure_iot_utilities.h"

extern volatile sig_atomic_t terminationRequired = false;

#include "parson.h" // used to parse Device Twin messages.

#include "mt3620_avnet_dev.h"
#include "SoilSensor\i2cAccess.h"
#include "SoilSensor\SoilMoistureI2cSensor.h"
#include "RelayClick\relay.h"
#include "time_utilities.h"

// File descriptor - initialized to invalid value
int i2cFd = -1;
// Default i2c addresses, update if customized.
static const I2C_DeviceAddress SoilMoistureI2cDefaultAddress1 = 0x20;
static const I2C_DeviceAddress SoilMoistureI2cDefaultAddress2 = 0x21;
static const I2C_DeviceAddress WaterTankI2cDefaultAddress = 0x22;

static const I2C_DeviceAddress moistureSensorsAddresses[3] = 
{ 
	SoilMoistureI2cDefaultAddress1, 
	SoilMoistureI2cDefaultAddress2, 
	WaterTankI2cDefaultAddress 
};

static char temperatureSensorNames[3][21] = 
{
	"Temperature1", 
	"Temperature2", 
	"TemperatureWaterTank"
};

static char capacitanceSensorNames[3][21] =
{
	"Capacitance1",
	"Capacitance2",
	"CapacitanceWaterTank"
};

// Relay Click definitions and variables.
static int relay1PinFd = -1;  //relay #1
static GPIO_Value_Type relay1Pin;
static int relay2PinFd = -1;  //relay #2
static GPIO_Value_Type relay2Pin;
static RELAY* relaysState;
static const int Relay1DefaultPollPeriodSeconds = 1;
static bool relay2WorkingHoursInEffect = false;
static int relay2WorkingHoursOn = -1;
static int relay2WorkingMinutesOn = -1;
static int relay2WorkingHoursOff = -1;
static int relay2WorkingMinutesOff = -1;
static int Relay1PulseSecondsSettingValue = 1;
static int Relay1PulseGraceSecondsSettingValue = -1;
static bool relay1InGracePeriod = false;
static int SoilMoistureCapacitanceThresholdSettingValue = -1;
static int WaterTankCapacitanceThresholdSettingValue = -1;

// Azure IoT Hub/Central defines.
#define SCOPEID_LENGTH 20
static char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
                                     // app_manifest.json, CmdArgs

static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context);
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                         size_t payloadSize, void *userContextCallback);
static void ParseHourMinuteFromJson(JSON_Object* Relay2OnTimeSetting, int *hours, int *minutes);
static void EnableRelay2WorkingHours(void);
static void SendTelemetryRelay1(void);
static void SendTelemetryRelay2(void);
static void TwinReportBoolState(const char *propertyName, bool propertyValue);
static void TwinReportStringState(const unsigned char* propertyName, const unsigned char* propertyValue);
static void ReportStatusCallback(int result, void *context);
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char *getAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SendTelemetry(const unsigned char *key, const unsigned char *value);
static void SetupAzureClient(void);
static void SendTelemetryMoisture(void);

// Initialization/Cleanup
static int InitPeripheralsAndHandlers(void);
static void InitializeSoilMoistureSensors(void);
static void GetMoistureSensorsInfo(void);
static int DirectMethodCall(const char* methodName, const char* payload, size_t payloadSize, char** responsePayload, size_t* responsePayloadSize);
static void InitializeRelays(void);
static void ClosePeripheralsAndHandlers(void);

// File descriptors - initialized to invalid value
// Buttons
static int sendMessageButtonGpioFd = -1;
static int sendOrientationButtonGpioFd = -1;

// LED
static int deviceTwinStatusLedGpioFd = -1;
static bool statusLedOn = false;

// Timer / polling
static int relayPollTimerFd = -1;
static int pulse1OneShotTimerFd = -1;
static int relay1GracePeriodTimerFd = -1;
static int buttonPollTimerFd = -1;
static int azureTimerFd = -1;
static int epollFd = -1;

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 5;
static const int AzureIoTMinReconnectPeriodSeconds = 10;
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 10;

static int azureIoTPollPeriodSeconds = -1;

static void SetRelayStates(RELAY* relaysPointer);

// Button state variables
static GPIO_Value_Type sendMessageButtonState = GPIO_Value_High;
static GPIO_Value_Type sendOrientationButtonState = GPIO_Value_High;

static bool IsButtonPressed(int fd, GPIO_Value_Type *oldState);
static void SendMessageButtonHandler(void);
static void SendOrientationButtonHandler(void);
static void ChangeSoilMoistureI2cAddress(I2C_DeviceAddress originAddress, I2C_DeviceAddress desiredAddress);
static void PulseRelay1(void);
static bool HasRelay1PulseGraceSecondsSettingValueBeenUpdated(void);
static bool HasSoilMoistureCapacitanceThresholdSettingValueBeenUpdated(void);
static bool HasWaterTankCapacitanceThresholdSettingValueBeenUpdated(void);
static void SwitchOnLampAtDayTime(void);
static bool deviceIsUp = false; // Orientation

static void RelayPollTimerEventHandler(EventData* eventData);
int MinutesFromHoursAndMinutes(int* hours, int* minutes);
static void Pulse1TimerEventHandler(EventData* eventData);
static void Relay1GracePeriodTimerEventHandler(EventData* eventData);
static void ButtonPollTimerEventHandler(EventData* eventData);
static void AzureTimerEventHandler(EventData *eventData);

// Event handler data structures. Only the event handler field needs to be populated.
static EventData relayPollEventData = { .eventHandler = &RelayPollTimerEventHandler };
static EventData pulse1EventData = { .eventHandler = &Pulse1TimerEventHandler };
static EventData relay1GracePeriodEventData = { .eventHandler = &Relay1GracePeriodTimerEventHandler };
static EventData buttonPollEventData = { .eventHandler = &ButtonPollTimerEventHandler };
static EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

// A null period to not start the timer when it is created with CreateTimerFdAndAddToEpoll.
static const struct timespec nullPeriod = { 0, 0 };

// Method identifiers
static const char Relay1PulseCommandName[] = "Relay1PulseCommand";
static const char Relay2PulseCommandName[] = "Relay2PulseCommand";

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
    terminationRequired = true;
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("IoT Hub/Central Application starting.\n");

    if (argc == 2) {
        Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
        strncpy(scopeId, argv[1], SCOPEID_LENGTH);
    } else {
        Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
        return -1;
    }

	// Note that the offset is positive if the local time zone is west of the Prime Meridian and
	// negative if it is east.
	SetLocalTimeZone("GMT-1"); // Norway

    if (InitPeripheralsAndHandlers() != 0) {
        terminationRequired = true;
    }

    // Main loop
    while (!terminationRequired) {
        if (WaitForEventAndCallHandler(epollFd) != 0) {
            terminationRequired = true;
        }
    }

    ClosePeripheralsAndHandlers();

    Log_Debug("Application exiting.\n");

    return 0;
}

void SetRelayStates(RELAY* relaysPointer)
{
	if (relaysPointer->relay1_status == 1)
		GPIO_SetValue(relay1PinFd, GPIO_Value_High);
	else
		GPIO_SetValue(relay1PinFd, GPIO_Value_Low);

	if (relaysPointer->relay2_status == 1)
		GPIO_SetValue(relay2PinFd, GPIO_Value_High);
	else
		GPIO_SetValue(relay2PinFd, GPIO_Value_Low);
}

/// <summary>
/// Button timer event:  Check the status of buttons A and B
/// </summary>
static void ButtonPollTimerEventHandler(EventData *eventData)
{
    if (ConsumeTimerFdEvent(buttonPollTimerFd) != 0) {
        terminationRequired = true;
        return;
    }
    SendMessageButtonHandler();
    SendOrientationButtonHandler();
}

/// <summary>
/// Relay timer event: Timer elapsed, evaluate and toggle relays.
/// </summary>
static void RelayPollTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(relayPollTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	//Log_Debug("RelayPollTimerEventHandler\n");
	if (iothubAuthenticated) 
	{
		PulseRelay1();
		SwitchOnLampAtDayTime();
	}
}

int MinutesFromHoursAndMinutes(int* hours, int* minutes) {
	return *hours * 60 + *minutes;
}

/// <summary>
/// Relay1 pulse timer event: Relay 1 pulse elapsed, toggle relay and clean up timer.
/// </summary>
static void Pulse1TimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(pulse1OneShotTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	Log_Debug("Pulse1TimerEventHandler\n");
	relaystate(relaysState, relay1_clr);
	SendTelemetryRelay1();
	struct timespec relay1GracePeriodSeconds = { Relay1PulseGraceSecondsSettingValue, 0 };
	SetTimerFdToSingleExpiry(relay1GracePeriodTimerFd, &relay1GracePeriodSeconds);
	if (relay1GracePeriodTimerFd > 0)
	{
		relay1InGracePeriod = true;
	}
}

/// <summary>
/// Relay1 grace period timer event: Relay 1 grace period elapsed, allow new pulse.
/// </summary>
static void Relay1GracePeriodTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(relay1GracePeriodTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	Log_Debug("Relay1GracePeriodTimerEventHandler\n");
	relay1InGracePeriod = false;
}

/// <summary>
/// Check if relay #1 is not already open.
/// Check if still in grace period.
/// Check if moisture sensor #1 or #2 is below configured threshold, i.e. soil is dry;
/// Check if moisture sensor #3 is above configured threshold, i.e. water tank is not empty;
/// Open relay #1 for configured number of seconds.
/// Send telemetry.
/// </summary>
static void PulseRelay1(void) 
{
	// Is in grace period or pulse already in progress?
	if (!relay1InGracePeriod 
		&& !relaystate(relaysState, relay1_rd) 
		&& HasRelay1PulseGraceSecondsSettingValueBeenUpdated()
		&& HasSoilMoistureCapacitanceThresholdSettingValueBeenUpdated()
		&& HasWaterTankCapacitanceThresholdSettingValueBeenUpdated())
	{
		// Water tank empty?
		if (GetCapacitance(moistureSensorsAddresses[2]) > WaterTankCapacitanceThresholdSettingValue) {
			// Plant 1 or 2 dry?
			if ((GetCapacitance(moistureSensorsAddresses[0]) < SoilMoistureCapacitanceThresholdSettingValue)
				|| (GetCapacitance(moistureSensorsAddresses[1]) < SoilMoistureCapacitanceThresholdSettingValue))
			{
				struct timespec pulse1DurationSeconds = { Relay1PulseSecondsSettingValue, 0 };
				SetTimerFdToSingleExpiry(pulse1OneShotTimerFd, &pulse1DurationSeconds);
				if (pulse1OneShotTimerFd > 0)
				{
					relaystate(relaysState, relay1_set);
					SendTelemetryRelay1();
					return;
				}
			}
		}
	}
}

/// <summary>
/// Wait for setting to be updated from IoT Central.
/// </summary>
static bool HasRelay1PulseGraceSecondsSettingValueBeenUpdated(void) {
	return Relay1PulseGraceSecondsSettingValue > -1;
}

/// <summary>
/// Wait for setting to be updated from IoT Central.
/// </summary>
static bool HasSoilMoistureCapacitanceThresholdSettingValueBeenUpdated(void) {
	return SoilMoistureCapacitanceThresholdSettingValue > -1;
}

/// <summary>
/// Wait for setting to be updated from IoT Central.
/// </summary>
static bool HasWaterTankCapacitanceThresholdSettingValueBeenUpdated(void) {
	return WaterTankCapacitanceThresholdSettingValue > -1;
}

/// <summary>
/// Turn on lamp using relay #2 if daytime.
/// </summary>
static void SwitchOnLampAtDayTime(void)
{
	if (relay2WorkingHoursInEffect) {

		struct timespec currentTime;
		struct tm* gmtTm;
		clock_gettime(CLOCK_REALTIME, &currentTime);
		gmtTm = localtime(&currentTime.tv_sec);
		int currentGmtMinutes = MinutesFromHoursAndMinutes(&gmtTm->tm_hour, &gmtTm->tm_min);
		int onTimeMinutes = MinutesFromHoursAndMinutes(&relay2WorkingHoursOn, &relay2WorkingMinutesOn);
		int offTimeMinutes = MinutesFromHoursAndMinutes(&relay2WorkingHoursOff, &relay2WorkingMinutesOff);

		// Is current clock before or after working hours?
		if (currentGmtMinutes < onTimeMinutes
			|| currentGmtMinutes >= offTimeMinutes) {
			// Switch off if not already off.
			if (relaystate(relaysState, relay2_rd)) 
			{
				relaystate(relaysState, relay2_clr);
				SendTelemetryRelay2();
			}
			
		}
		// Between working hours, switch on if not already on.
		else if(!relaystate(relaysState, relay2_rd))
		{
			relaystate(relaysState, relay2_set);
			SendTelemetryRelay2();
		}
	}
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(azureTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	bool isNetworkReady = false;
	if (Networking_IsNetworkingReady(&isNetworkReady) != -1) {
		if (isNetworkReady && !iothubAuthenticated) {
			SetupAzureClient();
		}
	}
	else {
		Log_Debug("Failed to get Network state\n");
	}

	if (iothubAuthenticated) {
		SendTelemetryMoisture();
		IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
	}
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    epollFd = CreateEpollFd();
    if (epollFd < 0) {
        return -1;
    }

    // Open button A GPIO as input
    Log_Debug("Opening SAMPLE_BUTTON_1 as input\n");
    sendMessageButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_1);
    if (sendMessageButtonGpioFd < 0) {
        Log_Debug("ERROR: Could not open button A: %s (%d).\n", strerror(errno), errno);
        return -1;
    }

    // Open button B GPIO as input
    Log_Debug("Opening SAMPLE_BUTTON_2 as input\n");
    sendOrientationButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_2);
    if (sendOrientationButtonGpioFd < 0) {
        Log_Debug("ERROR: Could not open button B: %s (%d).\n", strerror(errno), errno);
        return -1;
    }

    // LED 4 Blue is used to show Device Twin settings state
    Log_Debug("Opening SAMPLE_LED as output\n");
    deviceTwinStatusLedGpioFd =
        GPIO_OpenAsOutput(SAMPLE_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (deviceTwinStatusLedGpioFd < 0) {
        Log_Debug("ERROR: Could not open LED: %s (%d).\n", strerror(errno), errno);
        return -1;
    }

	// Initialize Soil Sensors
	Log_Debug("Opening ISU2 I2C\n");
	i2cFd = I2CMaster_Open(MT3620_ISU2_I2C);
	I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	I2CMaster_SetTimeout(i2cFd, 100);

	InitializeSoilMoistureSensors();
	
	GetMoistureSensorsInfo();

	// Uncomment to change address of sensor.
	//ChangeSoilMoistureI2cAddress(SoilMoistureI2cDefaultAddress1, WaterTankI2cDefaultAddress);

	relaysState = open_relay(SetRelayStates, InitializeRelays);
	// Set up relay check interval.
	struct timespec relay1CheckPeriod = { Relay1DefaultPollPeriodSeconds, 0 };
	relayPollTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &relay1CheckPeriod, &relayPollEventData, EPOLLIN);
	if (relayPollTimerFd < 0) {
		return -1;
	}

	// Set up a one-shot timer for pulse 1.
	pulse1OneShotTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &nullPeriod, &pulse1EventData, EPOLLIN);
	if (pulse1OneShotTimerFd < 0) {
		return -1;
	}

	// Set up a one-shot timer for realy 1 grace period.
	relay1GracePeriodTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &nullPeriod, &relay1GracePeriodEventData, EPOLLIN);
	if (relay1GracePeriodTimerFd < 0) {
		return -1;
	}

    // Set up a timer to poll for button events.
    struct timespec buttonPressCheckPeriod = {0, 1000 * 1000};
    buttonPollTimerFd =
        CreateTimerFdAndAddToEpoll(epollFd, &buttonPressCheckPeriod, &buttonPollEventData, EPOLLIN);
    if (buttonPollTimerFd < 0) {
        return -1;
    }

    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
    azureTimerFd =
        CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);
    if (azureTimerFd < 0) {
        return -1;
    }

	// Tell the system about the callback function to call when we receive a Direct Method message from Azure
	AzureIoT_SetDirectMethodCallback(&DirectMethodCall);

    return 0;
}

void InitializeSoilMoistureSensors(void)
{
	InitializeSoilSensor(SoilMoistureI2cDefaultAddress1, true);
	InitializeSoilSensor(SoilMoistureI2cDefaultAddress2, true);
	InitializeSoilSensor(WaterTankI2cDefaultAddress, true);
}

/// <summary>
///     Allocates and formats a string message on the heap.
/// </summary>
/// <param name="messageFormat">The format of the message</param>
/// <param name="maxLength">The maximum length of the formatted message string</param>
/// <returns>The pointer to the heap allocated memory.</returns>
static void* SetupHeapMessage(const char* messageFormat, size_t maxLength, ...)
{
	va_list args;
	va_start(args, maxLength);
	char* message =
		malloc(maxLength + 1); // Ensure there is space for the null terminator put by vsnprintf.
	if (message != NULL) {
		vsnprintf(message, maxLength, messageFormat, args);
	}
	va_end(args);
	return message;
}

/// <summary>
///     Direct Method callback function, called when a Direct Method call is received from the Azure
///     IoT Hub.
/// </summary>
/// <param name="methodName">The name of the method being called.</param>
/// <param name="payload">The payload of the method.</param>
/// <param name="responsePayload">The response payload content. This must be a heap-allocated
/// string, 'free' will be called on this buffer by the Azure IoT Hub SDK.</param>
/// <param name="responsePayloadSize">The size of the response payload content.</param>
/// <returns>200 HTTP status code if the method name is reconginized and the payload is correctly parsed;
/// 400 HTTP status code if the payload is invalid;</returns>
/// 404 HTTP status code if the method name is unknown.</returns>
static int DirectMethodCall(const char* methodName, const char* payload, size_t payloadSize, char** responsePayload, size_t* responsePayloadSize)
{
	Log_Debug("\nDirect Method called %s\n", methodName);

	int result = 404; // HTTP status code.

	if (payloadSize < 32) {

		// Declare a char buffer on the stack where we'll operate on a copy of the payload.  
		char directMethodCallContent[payloadSize + 1];

		// Prepare the payload for the response. This is a heap allocated null terminated string.
		// The Azure IoT Hub SDK is responsible of freeing it.
		*responsePayload = NULL;  // Reponse payload content.
		*responsePayloadSize = 0; // Response payload content size.


		// Look for the haltApplication method name.  This direct method does not require any payload, other than
		// a valid Json argument such as {}.

		if (strcmp(methodName, Relay1PulseCommandName) == 0) {

			// Log that the direct method was called and set the result to reflect success!
			Log_Debug("Relay1PulseCommand() Direct Method called\n");
			result = 200;

			// Construct the response message.  This response will be displayed in the cloud when calling the direct method
			static const char resetOkResponse[] =
				"{ \"success\" : true, \"message\" : \"Running Relay1PulseCommand\" }";
			size_t responseMaxLength = sizeof(resetOkResponse);
			*responsePayload = SetupHeapMessage(resetOkResponse, responseMaxLength);
			if (*responsePayload == NULL) {
				Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
				abort();
			}
			*responsePayloadSize = strlen(*responsePayload);

			relaystate(relaysState, relay1_set);
			TwinReportBoolState("Relay1Setting", relaystate(relaysState, relay1_rd));
			SendTelemetryRelay1();
			return result;
		}

		// Check to see if the setSensorPollTime direct method was called
		else if (strcmp(methodName, Relay2PulseCommandName) == 0) {

			// Log that the direct method was called and set the result to reflect success!
			Log_Debug("Relay2PulseCommand() Direct Method called\n");
			result = 200;

			// The payload should contain a JSON object such as: {"seconds": 5}
			if (directMethodCallContent == NULL) {
				Log_Debug("ERROR: Could not allocate buffer for direct method request payload.\n");
				abort();
			}

			// Copy the payload into our local buffer then null terminate it.
			memcpy(directMethodCallContent, payload, payloadSize);
			directMethodCallContent[payloadSize] = 0; // Null terminated string.

			JSON_Value* payloadJson = json_parse_string(directMethodCallContent);

			// Verify we have a valid JSON string from the payload
			if (payloadJson == NULL) {
				goto payloadError;
			}

			// Verify that the payloadJson contains a valid JSON object
			JSON_Object* pollTimeJson = json_value_get_object(payloadJson);
			if (pollTimeJson == NULL) {
				goto payloadError;
			}

			// Pull the Key: value pair from the JSON object, we're looking for {"seconds": <integer>}
			// Verify that the new timer is < 0
			int relay2PulseSeconds = (int)json_object_get_number(pollTimeJson, "Seconds");
			if (relay2PulseSeconds < 1) {
				goto payloadError;
			}
			else {

				Log_Debug("Relay 2 pulse seconds %d\n", relay2PulseSeconds);

				// Construct the response message.  This will be displayed in the cloud when calling the direct method
				static const char relay2PulseSecondsResponse[] =
					"{ \"success\" : true, \"message\" : \"Relay 2 pulse %d seconds\" }";
				size_t responseMaxLength = sizeof(relay2PulseSecondsResponse) + strlen(payload);
				*responsePayload = SetupHeapMessage(relay2PulseSecondsResponse, responseMaxLength, relay2PulseSeconds);
				if (*responsePayload == NULL) {
					Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
					abort();
				}
				*responsePayloadSize = strlen(*responsePayload);

				// Define a new timespec variable for the timer and change the timer period
				struct timespec newAccelReadPeriod = { .tv_sec = relay2PulseSeconds,.tv_nsec = 0 };
				//SetTimerFdToPeriod(accelTimerFd, &newAccelReadPeriod);
				return result;
			}
		}
		else {
			result = 404;
			Log_Debug("INFO: Direct Method called \"%s\" not found.\n", methodName);

			static const char noMethodFound[] = "\"method not found '%s'\"";
			size_t responseMaxLength = sizeof(noMethodFound) + strlen(methodName);
			*responsePayload = SetupHeapMessage(noMethodFound, responseMaxLength, methodName);
			if (*responsePayload == NULL) {
				Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
				abort();
			}
			*responsePayloadSize = strlen(*responsePayload);
			return result;
		}

	}
	else {
		Log_Debug("Payload size > 32 bytes, aborting Direct Method execution\n");
		goto payloadError;
	}

	// If there was a payload error, construct the 
	// response message and send it back to the IoT Hub for the user to see
payloadError:


	result = 400; // Bad request.
	Log_Debug("INFO: Unrecognised direct method payload format.\n");

	static const char noPayloadResponse[] =
		"{ \"success\" : false, \"message\" : \"request does not contain an identifiable "
		"payload\" }";

	size_t responseMaxLength = sizeof(noPayloadResponse) + strlen(payload);
	responseMaxLength = sizeof(noPayloadResponse);
	*responsePayload = SetupHeapMessage(noPayloadResponse, responseMaxLength);
	if (*responsePayload == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
		abort();
	}
	*responsePayloadSize = strlen(*responsePayload);

	return result;

}

/// <summary>
///     Define GPIOs for relay click as outputs and initialize as low.
/// </summary>
void InitializeRelays(void)
{
	relay1PinFd = GPIO_OpenAsOutput(SAMPLE_RELAY_1_CLICK_2, relay1Pin, GPIO_Value_Low);
	relay2PinFd = GPIO_OpenAsOutput(SAMPLE_RELAY_2_CLICK_2, relay2Pin, GPIO_Value_Low);
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    Log_Debug("Closing file descriptors\n");

    // Leave the LEDs off
    if (deviceTwinStatusLedGpioFd >= 0) {
        GPIO_SetValue(deviceTwinStatusLedGpioFd, GPIO_Value_High);
    }

	// Close relays
	close_relay(relaysState);
	GPIO_SetValue(relay1PinFd, GPIO_Value_Low);
	GPIO_SetValue(relay2PinFd, GPIO_Value_Low);

    CloseFdAndPrintError(buttonPollTimerFd, "ButtonTimer");
    CloseFdAndPrintError(azureTimerFd, "AzureTimer");
    CloseFdAndPrintError(sendMessageButtonGpioFd, "SendMessageButton");
    CloseFdAndPrintError(sendOrientationButtonGpioFd, "SendOrientationButton");
    CloseFdAndPrintError(deviceTwinStatusLedGpioFd, "StatusLed");
    CloseFdAndPrintError(epollFd, "Epoll");
	CloseFdAndPrintError(i2cFd, "I2C");
	CloseFdAndPrintError(relay1PinFd, "Relay 1");
	CloseFdAndPrintError(relay2PinFd, "Relay 2");
	CloseFdAndPrintError(relayPollTimerFd, "Relay 1");
	CloseFdAndPrintError(pulse1OneShotTimerFd, "Pulse 1");
	CloseFdAndPrintError(relay1GracePeriodTimerFd, "Relay 1 Grace Period");
}

/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
static void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
                                        IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
                                        void *userContextCallback)
{
    iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
    Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));

	if (iothubAuthenticated)
	{
		SendTelemetryRelay1();
		SendTelemetryRelay2();

		for (int i = 0; i < 3; i++)
		{
			char versionPropertyName[27];
			char addressPropertyName[27];
			snprintf(versionPropertyName, sizeof(versionPropertyName), "%s%d", "SoilSensorVersionProperty", i + 1);
			snprintf(addressPropertyName, sizeof(addressPropertyName), "%s%d", "SoilSensorAddressProperty", i + 1);
			char* version = malloc(5);
			char* address = malloc(5);
			snprintf(version, 5, "0x%02X", GetVersion(moistureSensorsAddresses[i]));
			snprintf(address, 5, "0x%02X", GetAddress(moistureSensorsAddresses[i]));
			TwinReportStringState(versionPropertyName, version);
			TwinReportStringState(addressPropertyName, address);
		}
	}
}

void GetMoistureSensorsInfo(void)
{
	for (int i = 0; i < 3; i++)
	{
		GetVersion(moistureSensorsAddresses[i]);
		GetAddress(moistureSensorsAddresses[i]);

		unsigned int soilSensor1Capacitance = GetCapacitance(moistureSensorsAddresses[i]);
		Log_Debug("Soil sensor (Address: %X) capacitance: %u\n", moistureSensorsAddresses[i], soilSensor1Capacitance);
		float soilSensor1Temperature = GetTemperature(moistureSensorsAddresses[i]);
		Log_Debug("Soil sensor (Address: %X) temperature: %.1f\n", moistureSensorsAddresses[i], soilSensor1Temperature);
	}
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
    if (iothubClientHandle != NULL)
        IoTHubDeviceClient_LL_Destroy(iothubClientHandle);

    AZURE_SPHERE_PROV_RETURN_VALUE provResult =
        IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
                                                                          &iothubClientHandle);
    Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
              getAzureSphereProvisioningResultString(provResult));

    if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {

        // If we fail to connect, reduce the polling frequency, starting at
        // AzureIoTMinReconnectPeriodSeconds and with a backoff up to
        // AzureIoTMaxReconnectPeriodSeconds
        if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
            azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
        } else {
            azureIoTPollPeriodSeconds *= 2;
            if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
                azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
            }
        }

        struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
        SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

        Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
                  azureIoTPollPeriodSeconds);
        return;
    }

    // Successfully connected, so make sure the polling frequency is back to the default
    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
    SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

    iothubAuthenticated = true;

    if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
                                        &keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
        Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
        return;
    }

    IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, TwinCallback, NULL);
    IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle,
                                                      HubConnectionStatusCallback, NULL);
	AzureIoT_SetupClient(iothubClientHandle);
}

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
///     Updates local state for 'showEvents' (bool).
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char *payload,
                         size_t payloadSize, void *userContextCallback)
{
    size_t nullTerminatedJsonSize = payloadSize + 1;
    char *nullTerminatedJsonString = (char *)malloc(nullTerminatedJsonSize);
    if (nullTerminatedJsonString == NULL) {
        Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
        abort();
    }

    // Copy the provided buffer to a null terminated buffer.
    memcpy(nullTerminatedJsonString, payload, payloadSize);
    // Add the null terminator at the end.
    nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

    JSON_Value *rootProperties = NULL;
    rootProperties = json_parse_string(nullTerminatedJsonString);
    if (rootProperties == NULL) {
        Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    JSON_Object *rootObject = json_value_get_object(rootProperties);
    JSON_Object *desiredProperties = json_object_dotget_object(rootObject, "desired");
    if (desiredProperties == NULL) {
        desiredProperties = rootObject;
    }

    // Handle the Device Twin Desired Properties here.
    JSON_Object *LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
    if (LEDState != NULL) {
        statusLedOn = (bool)json_object_get_boolean(LEDState, "value");
        GPIO_SetValue(deviceTwinStatusLedGpioFd,
                      (statusLedOn == true ? GPIO_Value_Low : GPIO_Value_High));
        TwinReportBoolState("StatusLED", statusLedOn);
    }

	// Relay 1 ON/OFF Setting
	JSON_Object* Relay1StateSetting = json_object_dotget_object(desiredProperties, "Relay1Setting");
	if (Relay1StateSetting != NULL) {
		bool tempStatusRelay = (bool)json_object_get_boolean(Relay1StateSetting, "value");
		relaystate(relaysState, tempStatusRelay ? relay1_set : relay1_clr);
		TwinReportBoolState("Relay1Setting", relaystate(relaysState, relay1_rd));
		SendTelemetryRelay1();
	}

	// Relay 2 ON/OFF Setting
	JSON_Object* Relay2StateSetting = json_object_dotget_object(desiredProperties, "Relay2Setting");
	if (Relay2StateSetting != NULL) {
		bool tempStatusRelay = (bool)json_object_get_boolean(Relay2StateSetting, "value");
		relaystate(relaysState, tempStatusRelay ? relay2_set : relay2_clr);
		TwinReportBoolState("Relay2Setting", relaystate(relaysState, relay2_rd));
		SendTelemetryRelay2();
	}

	// Relay 2 ON time Setting
	JSON_Object* Relay2OnTimeSetting = json_object_dotget_object(desiredProperties, "Relay2OnTimeSetting");
	if (Relay2OnTimeSetting != NULL) {
		ParseHourMinuteFromJson(Relay2OnTimeSetting, &relay2WorkingHoursOn, &relay2WorkingMinutesOn);
		EnableRelay2WorkingHours();
		TwinReportStringState("Relay2OnTimeSetting", json_object_get_string(Relay2OnTimeSetting, "value"));
	}

	// Relay 2 OFF time Setting
	JSON_Object* Relay2OffTimeSetting = json_object_dotget_object(desiredProperties, "Relay2OffTimeSetting");
	if (Relay2OffTimeSetting != NULL) {
		ParseHourMinuteFromJson(Relay2OffTimeSetting, &relay2WorkingHoursOff, &relay2WorkingMinutesOff);
		EnableRelay2WorkingHours();
		TwinReportStringState("Relay2OffTimeSetting", json_object_get_string(Relay2OffTimeSetting, "value"));
	}

	// Relay 1 pulse duration seconds Setting
	JSON_Object* Relay1PulseSecondsSetting = json_object_dotget_object(desiredProperties, "Relay1PulseSecondsSetting");
	if (Relay1PulseSecondsSetting != NULL) {
		Relay1PulseSecondsSettingValue = (int)json_object_get_number(Relay1PulseSecondsSetting, "value");
		char* relay1PulseSecondsSettingBuffer = malloc(3);
		snprintf(relay1PulseSecondsSettingBuffer, 2, "%d", Relay1PulseSecondsSettingValue);
		TwinReportStringState("Relay1PulseSecondsSetting", relay1PulseSecondsSettingBuffer);
	}

	// Relay 1 pulse duration seconds Setting
	JSON_Object* Relay1PulseGraceSecondsSetting = json_object_dotget_object(desiredProperties, "Relay1PulseGraceSecondsSetting");
	if (Relay1PulseGraceSecondsSetting != NULL) {
		Relay1PulseGraceSecondsSettingValue = (int)json_object_get_number(Relay1PulseGraceSecondsSetting, "value");
		char* relay1PulseGraceSecondsSettingBuffer = malloc(7);
		snprintf(relay1PulseGraceSecondsSettingBuffer, 6, "%d", Relay1PulseGraceSecondsSettingValue);
		TwinReportStringState("Relay1PulseGraceSecondsSetting", relay1PulseGraceSecondsSettingBuffer);
	}

	// Soil moisture threshold Setting
	JSON_Object* SoilMoistureCapacitanceThresholdSetting = json_object_dotget_object(desiredProperties, "SoilMoistureCapacitanceThresholdSetting");
	if (SoilMoistureCapacitanceThresholdSetting != NULL) {
		SoilMoistureCapacitanceThresholdSettingValue = (int)json_object_get_number(SoilMoistureCapacitanceThresholdSetting, "value");
		char* soilMoistureCapacitanceThresholdSettingBuffer = malloc(7);
		snprintf(soilMoistureCapacitanceThresholdSettingBuffer, 6, "%d", SoilMoistureCapacitanceThresholdSettingValue);
		TwinReportStringState("SoilMoistureCapacitanceThresholdSetting", soilMoistureCapacitanceThresholdSettingBuffer);
	}

	// Water tank threshold Setting
	JSON_Object* WaterTankCapacitanceThresholdSetting = json_object_dotget_object(desiredProperties, "WaterTankCapacitanceThresholdSetting");
	if (WaterTankCapacitanceThresholdSetting != NULL) {
		WaterTankCapacitanceThresholdSettingValue = (int)json_object_get_number(WaterTankCapacitanceThresholdSetting, "value");
		char* waterTankCapacitanceThresholdSettingBuffer = malloc(7);
		snprintf(waterTankCapacitanceThresholdSettingBuffer, 6, "%d", WaterTankCapacitanceThresholdSettingValue);
		TwinReportStringState("WaterTankCapacitanceThresholdSetting", waterTankCapacitanceThresholdSettingBuffer);
	}

cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);
    free(nullTerminatedJsonString);
}

void ParseHourMinuteFromJson(JSON_Object* Relay2OnTimeSetting, int *hours, int *minutes)
{
	size_t nullTerminatedJsonDateTimeSize = 24 + 1;
	int jsonDateTimeHourStartIndex = 11;
	int jsonDateTimeMinuteStartIndex = 14;
	char relay2OnTimeHourBuffer[2 + 1] = "00";
	char relay2OnTimeMinuteBuffer[2 + 1] = "00";
	char* nullTerminatedRelay2OnDateTime = (char*)malloc(nullTerminatedJsonDateTimeSize);
	// Copy the provided buffer to a null terminated buffer.
	memcpy(nullTerminatedRelay2OnDateTime, json_object_get_string(Relay2OnTimeSetting, "value"), nullTerminatedJsonDateTimeSize);
	// Add the null terminator at the end.
	nullTerminatedRelay2OnDateTime[nullTerminatedJsonDateTimeSize - 1] = 0;
	memcpy(relay2OnTimeHourBuffer, &nullTerminatedRelay2OnDateTime[jsonDateTimeHourStartIndex], 2);
	memcpy(relay2OnTimeMinuteBuffer, &nullTerminatedRelay2OnDateTime[jsonDateTimeMinuteStartIndex], 2);
	*hours = atoi(relay2OnTimeHourBuffer);
	*minutes = atoi(relay2OnTimeMinuteBuffer);
	free(nullTerminatedRelay2OnDateTime);
}

/// <summary>
///     If valid on- and off-time is configured in IoT Central, enable checks to toggle relay 2.
/// </summary>
static void EnableRelay2WorkingHours(void)
{
	// Should perform a lot of validation	
	if (relay2WorkingHoursOn > -1 
		&& relay2WorkingMinutesOn > -1
		&& relay2WorkingHoursOff > -1
		&& relay2WorkingMinutesOff > -1) 
	{
		// Is Off-time after On-time?
		if ((relay2WorkingHoursOff * 60 + relay2WorkingMinutesOff) 
			<= (relay2WorkingHoursOn * 60 + relay2WorkingMinutesOn)) {
			Log_Debug("WARNING: Relay 2 working hours off-time is not after on-time\n");
			relay2WorkingHoursInEffect = false;
			return;
		}
		relay2WorkingHoursInEffect = true;
	}
}

static void SendTelemetryRelay1(void)
{
	SendTelemetry("Relay1State", relaystate(relaysState, relay1_rd) == 1 ? "On" : "Off");
}

static void SendTelemetryRelay2(void)
{
	SendTelemetry("Relay2State", relaystate(relaysState, relay2_rd) == 1 ? "On" : "Off");
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
static const char *GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
    static char *reasonString = "unknown reason";
    switch (reason) {
    case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
        reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
        break;
    case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
        break;
    case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
        reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
        break;
    case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
        reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
        break;
    case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
        break;
    case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
        reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
        break;
    case IOTHUB_CLIENT_CONNECTION_OK:
        reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
        break;
    }
    return reasonString;
}

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char *getAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
    switch (provisioningResult.result) {
    case AZURE_SPHERE_PROV_RESULT_OK:
        return "AZURE_SPHERE_PROV_RESULT_OK";
    case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
        return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
    case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
    case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
    default:
        return "UNKNOWN_RETURN_VALUE";
    }
}

/// <summary>
///     Sends telemetry to IoT Hub
/// </summary>
/// <param name="key">The telemetry item to update</param>
/// <param name="value">new telemetry value</param>
static void SendTelemetry(const unsigned char *key, const unsigned char *value)
{
    static char eventBuffer[100] = {0};
    static const char *EventMsgTemplate = "{ \"%s\": \"%s\" }";
    int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, key, value);
    if (len < 0)
        return;

    Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

    IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);

    if (messageHandle == 0) {
        Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
        return;
    }

    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
                                             /*&callback_param*/ 0) != IOTHUB_CLIENT_OK) {
        Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
    } else {
        Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
    }

    IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Callback confirming message delivered to IoT Hub.
/// </summary>
/// <param name="result">Message delivery status</param>
/// <param name="context">User specified context</param>
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *context)
{
    //Log_Debug("INFO: Message received by IoT Hub. Result is: %d\n", result);
}

/// <summary>
///     Creates and enqueues a report containing the name and value pair of a Device Twin reported
///     property. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
/// </summary>
/// <param name="propertyName">the IoT Hub Device Twin property name</param>
/// <param name="propertyValue">the IoT Hub Device Twin property value</param>
static void TwinReportBoolState(const char *propertyName, bool propertyValue)
{
    if (iothubClientHandle == NULL) {
        Log_Debug("ERROR: client not initialized\n");
    } else {
        static char reportedPropertiesString[30] = {0};
        int len = snprintf(reportedPropertiesString, 30, "{\"%s\":%s}", propertyName,
                           (propertyValue == true ? "true" : "false"));
        if (len < 0)
            return;

        if (IoTHubDeviceClient_LL_SendReportedState(
                iothubClientHandle, (unsigned char *)reportedPropertiesString,
                strlen(reportedPropertiesString), ReportStatusCallback, 0) != IOTHUB_CLIENT_OK) {
            Log_Debug("ERROR: failed to set reported state for '%s'.\n", propertyName);
        } else {
            Log_Debug("INFO: Reported state for '%s' to value '%s'.\n", propertyName,
                      (propertyValue == true ? "true" : "false"));
        }
    }
}

/// <summary>
///     Creates and enqueues a report containing the name and value pair of a Device Twin reported
///     property. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
/// </summary>
/// <param name="propertyName">the IoT Hub Device Twin property name</param>
/// <param name="propertyValue">the IoT Hub Device Twin property value</param>
static void TwinReportStringState(const unsigned char* propertyName, const unsigned char* propertyValue)
{
	if (iothubClientHandle == NULL) {
		Log_Debug("ERROR: client not initialized\n");
	}
	else {
		static char reportedPropertiesString[60] = { 0 };
		int len = snprintf(reportedPropertiesString, 60, "{\"%s\":\"%s\"}", propertyName,
			propertyValue);
		if (len < 0)
			return;
		Log_Debug("Sending IoT Hub Message Reported state: %s\n", reportedPropertiesString);
		if (IoTHubDeviceClient_LL_SendReportedState(
			iothubClientHandle, (unsigned char*)reportedPropertiesString,
			strlen(reportedPropertiesString), ReportStatusCallback, 0) != IOTHUB_CLIENT_OK) {
			Log_Debug("ERROR: failed to set reported state for '%s'.\n", propertyName);
		}
		else {
			Log_Debug("INFO: Reported state for '%s' to value '%s'.\n", propertyName,
				propertyValue);
		}
	}
}

/// <summary>
///     Callback invoked when the Device Twin reported properties are accepted by IoT Hub.
/// </summary>
static void ReportStatusCallback(int result, void *context)
{
    Log_Debug("INFO: Device Twin reported properties update result: HTTP status code %d\n", result);
}

/// <summary>
///     Collect sensor readings and send to IoT Central.
/// </summary>
void SendTelemetryMoisture(void)
{
	for (int i = 0; i < 3; i++)
	{
		float sensorTemperature = -1;
		unsigned int sensorCapacitance = 0;

		// Only read sensors when motor is idle. The motor generates a lot of noise, see project description.
		if (!relaystate(relaysState, relay1_rd))
		{
			if (IsBusy(moistureSensorsAddresses[i]))
			{
				Log_Debug("Soil sensor is busy\n");
			}
			else {
				sensorTemperature = GetTemperature(moistureSensorsAddresses[i]);
				if (sensorTemperature > 1000) {
					// Must be false reading, restart sensor.
					Log_Debug("ERROR: Soil sensor (Address: %X) temperature: %.1f\n", moistureSensorsAddresses[i], sensorTemperature);
					terminationRequired = true;
					return;
				}
				Log_Debug("Soil sensor (Address: %X) temperature: %.1f\n", moistureSensorsAddresses[i], sensorTemperature);
			}
			if (IsBusy(moistureSensorsAddresses[i]))
			{
				Log_Debug("Soil sensor is busy\n");
			}
			else {
				sensorCapacitance = GetCapacitance(moistureSensorsAddresses[i]);
				if (sensorCapacitance > 1000) {
					// Must be false reading, restart sensor.
					Log_Debug("ERROR: Soil sensor (Address: %X) temperature: %.1f\n", moistureSensorsAddresses[i], sensorTemperature);
					terminationRequired = true;
					return;
				}
				Log_Debug("Soil sensor (Address: %X) capacitance: %u\n", moistureSensorsAddresses[i], sensorCapacitance);
			}

			if (sensorTemperature > -1) {
				char tempBuffer[20] = { 0 };
				int len = snprintf(tempBuffer, 20, "%3.1f", sensorTemperature);
				if (len > 0)
					SendTelemetry(temperatureSensorNames[i], tempBuffer);
			}

			if (sensorCapacitance > 0) {
				char tempBuffer[20] = { 0 };
				int len = snprintf(tempBuffer, 20, "%u", sensorCapacitance);
				if (len > 0)
					SendTelemetry(capacitanceSensorNames[i], tempBuffer);
			}
		}
		else {
			Log_Debug("Relay 1 is busy\n");
		}
	}
}

/// <summary>
///     Check whether a given button has just been pressed.
/// </summary>
/// <param name="fd">The button file descriptor</param>
/// <param name="oldState">Old state of the button (pressed or released)</param>
/// <returns>true if pressed, false otherwise</returns>
static bool IsButtonPressed(int fd, GPIO_Value_Type *oldState)
{
    bool isButtonPressed = false;
    GPIO_Value_Type newState;
    int result = GPIO_GetValue(fd, &newState);
    if (result != 0) {
        Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
        terminationRequired = true;
    } else {
        // Button is pressed if it is low and different than last known state.
        isButtonPressed = (newState != *oldState) && (newState == GPIO_Value_Low);
        *oldState = newState;
    }

    return isButtonPressed;
}

/// <summary>
/// Pressing button A will:
///     Send a 'Button Pressed' event to Azure IoT Central
/// </summary>
static void SendMessageButtonHandler(void)
{
    if (IsButtonPressed(sendMessageButtonGpioFd, &sendMessageButtonState)) {
        SendTelemetry("ButtonPress", "True");
    }
}

/// <summary>
/// Pressing button B will:
///     Send an 'Orientation' event to Azure IoT Central
/// </summary>
static void SendOrientationButtonHandler(void)
{
    if (IsButtonPressed(sendOrientationButtonGpioFd, &sendOrientationButtonState)) {
        deviceIsUp = !deviceIsUp;
        SendTelemetry("Orientation", deviceIsUp ? "Up" : "Down");
    }
}

/// <summary>
/// Change address of a soil sensor. Do not change addresses with more than 
/// one sensor connected with the same address.
/// </summary>
static void ChangeSoilMoistureI2cAddress(I2C_DeviceAddress originAddress, I2C_DeviceAddress desiredAddress)
{
	SetAddress(originAddress, desiredAddress, true);
	//GetAddress(originAddress);
	//GetAddress(desiredAddress);
	while(true)
	{
		// Program should not continue, stop and update new addresses.
	}
}