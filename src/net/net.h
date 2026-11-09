// Wi-Fi and updates. Wi-Fi is off except while provisioning or checking for a
// release: the audio core should not share time with the radio. Provisioning
// uses WiFiManager's captive portal (join "Eridian-Translator", open
// 192.168.4.1). Credentials live in the ESP32's own Wi-Fi NVS.
#pragma once
#include <stdint.h>

namespace net {

void begin();
void tick(uint32_t nowMs);          // portal processing, scheduled checks, state machine
void startWifiSetup();              // opens the portal for up to 3 minutes
void checkUpdatesNow();             // connect, check, update if newer, disconnect
void forgetWifi();                  // erase saved credentials
bool busy();                        // portal open or an update in progress
const char *statusLine();           // for the settings screen
bool hasCredentials();

}  // namespace net
