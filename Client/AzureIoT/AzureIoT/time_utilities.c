#include "time_utilities.h"

extern volatile sig_atomic_t terminationRequired;

/// <summary>
///     Print the time in both UTC time zone and local time zone.
/// </summary>
void PrintTime(void)
{
	// Ask for CLOCK_REALTIME to obtain the current system time. This is not to be confused with the
	// hardware RTC used below to persist the time.
	struct timespec currentTime;
	if (clock_gettime(CLOCK_REALTIME, &currentTime) == -1) {
		Log_Debug("ERROR: clock_gettime failed with error code: %s (%d).\n", strerror(errno),
			errno);
		terminationRequired = true;
		return;
	}
	else {
		char displayTimeBuffer[26];
		if (!asctime_r((gmtime(&currentTime.tv_sec)), (char* restrict) & displayTimeBuffer)) {
			Log_Debug("ERROR: asctime_r failed with error code: %s (%d).\n", strerror(errno),
				errno);
			terminationRequired = true;
			return;
		}
		Log_Debug("UTC:            %s", displayTimeBuffer);

		if (!asctime_r((localtime(&currentTime.tv_sec)), (char* restrict) & displayTimeBuffer)) {
			Log_Debug("ERROR: asctime_r failed with error code: %s (%d).\n", strerror(errno),
				errno);
			terminationRequired = true;
			return;
		}

		// Remove the new line at the end of 'displayTimeBuffer'
		displayTimeBuffer[strlen(displayTimeBuffer) - 1] = '\0';
		size_t tznameIndex = ((localtime(&currentTime.tv_sec))->tm_isdst) ? 1 : 0;
		Log_Debug("Local time:     %s %s\n", displayTimeBuffer, tzname[tznameIndex]);
	}
}

/// <summary>
///     Check whether time sync is enabled on the device. If it is enabled, the current time may be
///     overwritten by NTP.
/// </summary>
static void CheckTimeSyncState(void)
{
	bool isTimeSyncEnabled = false;
	int result = Networking_TimeSync_GetEnabled(&isTimeSyncEnabled);
	if (result != 0) {
		Log_Debug("ERROR: Networking_TimeSync_GetEnabled failed: %s (%d).\n", strerror(errno),
			errno);
		return;
	}

	// If time sync is enabled, NTP can reset the time
	if (isTimeSyncEnabled) {
		Log_Debug(
			"The device's NTP time-sync service is enabled. This means the current time may be "
			"overwritten by NTP.\n");
	}
	else {
		Log_Debug(
			"NTP time-sync service is disabled on the device. The current time will not be "
			"overwritten by NTP.\nUnless RTC is used and powered by external source current time will not be synchronized.");
	}
}

void SetLocalTimeZone(const char* timeZone)
{
	CheckTimeSyncState();
	Log_Debug("\nTime before setting time zone:\n");
	PrintTime();
	// Note that the offset is positive if the local time zone is west of the Prime Meridian and
	// negative if it is east.
	Log_Debug("\nSetting local time zone to: PST+8:\n");
	int result = setenv("TZ", timeZone, 1);
	if (result == -1) {
		Log_Debug("ERROR: setenv failed with error code: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
	}
	else {
		tzset();
		PrintTime();
	}
}