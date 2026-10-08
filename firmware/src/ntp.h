#pragma once
#include <time.h>
#include <stdbool.h>

void      ntp_init_tz();     // set Europe/Oslo rules (call at boot)
void      ntp_sync();        // blocking, up to 10 s
bool      ntp_is_synced();
struct tm ntp_get_local_time();
