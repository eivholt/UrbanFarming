#pragma once
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <applibs/log.h>
#include <applibs/networking.h>

void PrintTime(void);
void SetLocalTimeZone(const char* timeZone);

