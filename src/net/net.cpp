#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <stdio.h>

#include "../brain/brain.h"
#include "../ota/ota.h"
#include "../synth/synth_engine.h"
#include "../ui/settings_screen.h"

namespace net {
namespace {

enum class Phase : uint8_t { Idle, Portal, Connecting, Checking, Restarting };

Phase g_phase = Phase::Idle;
WiFiManager g_wm;
bool g_portalStarted = false;
uint32_t g_phaseSince = 0;
uint32_t g_nextAutoCheck = 0;
bool g_manual = false;
char g_status[96] = "";
bool g_haveCreds = false;  // cached at begin() and after setup/forget; reading it toggles the radio
constexpr const char *AP_NAME = "Eridian-Translator";
constexpr uint32_t PORTAL_TIMEOUT_MS = 180000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t AUTO_CHECK_FIRST_MS = 90000;        // after boot, once things are quiet
constexpr uint32_t AUTO_CHECK_PERIOD_MS = 24UL * 3600000UL;
constexpr uint32_t AUTO_CHECK_RETRY_MS = 3600000UL;

void setStatus(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_status, sizeof g_status, fmt, ap);
  va_end(ap);
  Serial.printf("net: %s\n", g_status);
  ui::settings::setStatus(g_status);
}

void setPhase(Phase p) {
  g_phase = p;
  g_phaseSince = millis();
}

// Reads the stored SSID once. The driver only exposes its saved config once a
// connection with it has been requested, so start one and stop it again.
void refreshCreds() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // stored credentials, if any
  uint32_t t0 = millis();
  while (WiFi.SSID().length() == 0 && millis() - t0 < 300) delay(10);
  g_haveCreds = WiFi.SSID().length() > 0;
  Serial.printf("net: saved Wi-Fi %s\n", g_haveCreds ? WiFi.SSID().c_str() : "(none)");
  WiFi.disconnect(true, false);  // radio off, credentials kept
  WiFi.mode(WIFI_OFF);
}

void radioOff() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

void runCheck() {
  setPhase(Phase::Checking);
  synth::noteOff();
  ota::Ui ui;
  ui.message = [](const char *title, const String &line) { setStatus("%s %s", title, line.c_str()); };
  ui.progress = [](int done, int total) {
    static int last = -1;
    int pct = total > 0 ? (int)((int64_t)done * 100 / total) : 0;
    if (pct != last) {
      last = pct;
      setStatus("Downloading %d%%", pct);
    }
  };
  String newVersion;
  ota::Result r = ota::check(g_manual, ui, newVersion);
  switch (r) {
    case ota::UPDATED:
      setStatus("Updated to %s, restarting", newVersion.c_str());
      setPhase(Phase::Restarting);
      return;
    case ota::UP_TO_DATE:
      setStatus("Up to date (%s)", ota::versionLabel().c_str());
      break;
    case ota::NO_RELEASE:
      setStatus("No release published yet (%s)", ota::versionLabel().c_str());
      break;
    case ota::FAILED:
      setStatus("Update check failed, still %s", ota::versionLabel().c_str());
      break;
  }
  g_nextAutoCheck = millis() + (r == ota::FAILED ? AUTO_CHECK_RETRY_MS : AUTO_CHECK_PERIOD_MS);
  radioOff();
  setPhase(Phase::Idle);
}

void startConnect(bool manual) {
  g_manual = manual;
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // stored credentials
  uint32_t t0 = millis();
  while (WiFi.SSID().length() == 0 && millis() - t0 < 300) delay(10);
  g_haveCreds = WiFi.SSID().length() > 0;
  if (!g_haveCreds) {
    setStatus("No Wi-Fi saved. Use Wi-Fi setup first.");
    radioOff();
    setPhase(Phase::Idle);
    return;
  }
  setStatus("Connecting to %s", WiFi.SSID().c_str());
  setPhase(Phase::Connecting);
}

}  // namespace

void begin() {
  WiFi.persistent(true);
  refreshCreds();
  g_nextAutoCheck = millis() + AUTO_CHECK_FIRST_MS;
  g_wm.setConfigPortalBlocking(false);
  g_wm.setConfigPortalTimeout(PORTAL_TIMEOUT_MS / 1000);
  g_wm.setConnectTimeout(20);
  g_wm.setTitle("Eridian Translator");
  snprintf(g_status, sizeof g_status, "%s", g_haveCreds ? "Wi-Fi saved" : "No Wi-Fi saved");
}

bool hasCredentials() { return g_haveCreds; }

void startWifiSetup() {
  if (g_phase != Phase::Idle) return;
  setStatus("Join Wi-Fi '%s', open 192.168.4.1", AP_NAME);
  WiFi.mode(WIFI_AP_STA);
  g_portalStarted = g_wm.startConfigPortal(AP_NAME);
  setPhase(Phase::Portal);
}

void checkUpdatesNow() {
  if (g_phase != Phase::Idle) return;
  startConnect(true);
}

void forgetWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  g_wm.resetSettings();
  radioOff();
  g_haveCreds = false;
  setStatus("Wi-Fi forgotten");
}

bool busy() { return g_phase != Phase::Idle; }
const char *statusLine() { return g_status; }

void tick(uint32_t now) {
  switch (g_phase) {
    case Phase::Idle:
      if (!g_manual && now >= g_nextAutoCheck && hasCredentials() && !brain::speaking()) {
        startConnect(false);
      } else if (now >= g_nextAutoCheck && !hasCredentials()) {
        g_nextAutoCheck = now + AUTO_CHECK_RETRY_MS;  // look again later in case Wi-Fi gets set up
      }
      break;
    case Phase::Portal:
      g_wm.process();
      if (WiFi.status() == WL_CONNECTED) {
        setStatus("Connected to %s", WiFi.SSID().c_str());
        g_haveCreds = true;
        g_manual = true;
        if (g_wm.getConfigPortalActive()) g_wm.stopConfigPortal();
        runCheck();  // a fresh setup goes straight into a check
      } else if (!g_wm.getConfigPortalActive()) {
        // WiFiManager enforces the timeout itself and has already torn the
        // portal down. Calling stopConfigPortal() again here crashed (it frees
        // its web server twice), so only report and tidy the radio.
        setStatus("Wi-Fi setup timed out");
        radioOff();
        setPhase(Phase::Idle);
      }
      break;
    case Phase::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        setStatus("Checking for updates");
        runCheck();
      } else if (millis() - g_phaseSince > CONNECT_TIMEOUT_MS) {
        setStatus("Could not join %s", WiFi.SSID().c_str());
        g_nextAutoCheck = now + AUTO_CHECK_RETRY_MS;
        radioOff();
        setPhase(Phase::Idle);
      }
      break;
    case Phase::Checking:
      break;  // runCheck() is synchronous
    case Phase::Restarting:
      if (millis() - g_phaseSince > 1500) ESP.restart();
      break;
  }
}

}  // namespace net
