#pragma once

#include <time.h>

// Formats a time_t into a human-readable string.
// Returns a pointer to ctime()'s internal static buffer with trailing newline removed.
// If remove_day_name=1 (default), also drops the leading "Www " day-name prefix.
// Input:  time_t from time() — e.g. ctime result "Mon Jan  1 12:34:56 2024\n"
// Output: static buffer — "Jan  1 12:34:56 2024" (or "Mon Jan  1 12:34:56 2024" if remove_day_name=0)
//
// ⚠️  WARNING: Returns pointer to ctime()'s reused static buffer.
//    Each call to ctime() overwrites the buffer. Safe only for immediate use:
//      Serial.printf("Time: %s\n", formatTime(now));  // OK - used immediately
//    NOT safe for sequential storage:
//      const char *t1 = formatTime(time1);
//      const char *t2 = formatTime(time2);  // Overwrites t1's data
//      Serial.println(t1);  // t1 now contains t2's data
char* formatTime(time_t t, uint8_t remove_day_name = 1);
