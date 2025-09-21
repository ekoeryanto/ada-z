
/* Definitions for externs declared in include/config.h */
#include "config.h"

// Optional HTTP headers for notification requests. Change values as needed.
const HttpHeader HTTP_NOTIFICATION_HEADERS[] = {
    {"Authorization", "Bearer your_auth_token_here"},
    {"X-Custom-Header", "MyCustomValue"}
};

const int NUM_HTTP_NOTIFICATION_HEADERS = sizeof(HTTP_NOTIFICATION_HEADERS) / sizeof(HTTP_NOTIFICATION_HEADERS[0]);
