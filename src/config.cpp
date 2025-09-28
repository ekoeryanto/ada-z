
/* Definitions for externs declared in include/config.h */
#include "config.h"

// Default SNTP pool list. Update to match local infrastructure if needed.
const char* NTP_SERVERS[] = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com"
};

const size_t NTP_SERVER_COUNT = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);

// Optional HTTP headers for notification requests. Change values as needed.
const HttpHeader HTTP_NOTIFICATION_HEADERS[] = {
    {"Authorization", "Bearer your_auth_token_here"},
    {"X-Custom-Header", "MyCustomValue"}
};

const int NUM_HTTP_NOTIFICATION_HEADERS = sizeof(HTTP_NOTIFICATION_HEADERS) / sizeof(HTTP_NOTIFICATION_HEADERS[0]);
