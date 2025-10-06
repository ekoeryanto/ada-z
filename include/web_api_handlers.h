#ifndef WEB_API_HANDLERS_H
#define WEB_API_HANDLERS_H

#include <ESPAsyncWebServer.h>

// Register grouped handlers on the provided server instance
void registerSystemHandlers(AsyncWebServer *server);

#endif // WEB_API_HANDLERS_H
