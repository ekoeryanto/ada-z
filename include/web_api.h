#ifndef WEB_API_H
#define WEB_API_H

#include <Arduino.h>
#include <WebServer.h>

// Function to set up the web server and its API endpoints
void setupWebServer();

// Function to handle web server clients (call in loop)
void handleWebServerClients();

#endif // WEB_API_H
