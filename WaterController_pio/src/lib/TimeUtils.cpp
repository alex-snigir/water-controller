// TimeUtils - Time formatting utilities
//
// v0.1 20260613 - Initial release

#include <time.h>
#include <string.h>
#include "TimeUtils.h"

char* formatTime(time_t t, uint8_t remove_day_name) {
    // ⚠️  WARNING: ctime() returns a pointer to a static buffer that is reused on each call.
    // This function returns a pointer into that buffer. Each ctime() call overwrites it.
    // Safe only for immediate consumption (e.g., Serial.printf, LOG). Do not store the pointer.
    // Note: ctime() format: "Www Mmm dd hh:mm:ss yyyy\n" = 26 chars is the buffer size
    char *s = ctime(&t);
    s[strlen(s) - 1] = '\0';  // Remove trailing newline
    return remove_day_name ? &s[4] : s;  // Optionally skip leading "Www " day-name
}
