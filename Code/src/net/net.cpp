#include "net.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "bell.h"
#include "config.h"
#include "face.h"
#include "mood.h"
#include "usage.h"
#include "views.h"
#include "secrets.h"
#include "tuning.h"

// The settings page is written as its own file. __has_include keeps the
// firmware buildable while that file is still being authored.
#if __has_include("webpage.h")
#include "webpage.h"
#define HAVE_CONFIG_PAGE 1
#endif

namespace net {
namespace {

// The synchronous core WebServer rather than ESPAsyncWebServer: these handlers
// are three lines each, it costs no extra dependency, and rendering lives in a
// higher-priority task on the other core, so a blocking handler here cannot
// touch the frame budget.
WebServer s_server(80);

uint32_t s_next_retry = 0;
uint32_t s_backoff_ms = WIFI_RETRY_MS;
bool s_was_connected = false;
uint8_t s_attempts = 0;
char s_ip[16] = "0.0.0.0";
bool s_reboot_pending = false;
uint32_t s_reboot_at = 0;

// After this many failed joins the board stops assuming the network exists and
// hosts its own, so the settings page is always reachable. Without it a wrong
// password compiled in at flash time would be unrecoverable without a reflash —
// which is exactly what this whole config system exists to avoid.
const uint8_t kAttemptsBeforeAp = 4;

struct EventRoute {
  const char *path;
  mood::Event event;
};

// The raw facts a hook can report. Deliberately not moods — the board decides
// how it feels about these.
const EventRoute kRoutes[] = {
    {"/e/prompt_submitted", mood::Event::PromptSubmitted},
    {"/e/turn_finished",    mood::Event::TurnFinished},
    {"/e/needs_input",      mood::Event::NeedsInput},
    {"/e/idle",             mood::Event::Idle},
    {"/e/error",            mood::Event::Error},
    {"/e/session_started",  mood::Event::SessionStarted},
    {"/e/session_ended",    mood::Event::SessionEnded},
    {"/e/answered",         mood::Event::Answered},
    {"/e/limit_reached",    mood::Event::LimitReached},
};

// Pick the strongest BSSID advertising our SSID and bind to it explicitly.
//
// The gateway is fc:52:8d:74:3f:95 and there is a second node at ...:9a — the
// same OUI with an adjacent address, i.e. a mesh. Mesh systems steer clients
// between nodes and bands, and an ESP32 handles being steered badly: the
// symptom is exactly the ASSOC_LEAVE we keep seeing, the access point telling
// the board to go away. Naming a BSSID and channel stops the board chasing
// whichever node answered first.
bool connect_bound() {
  const char *ssid = config::get_str("wifi_ssid");
  const char *pass = config::get_str("wifi_pass");
  const int n = WiFi.scanNetworks(false, false, false, 300);
  int best = -1;
  int32_t best_rssi = -127;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != String(ssid)) continue;
    if (WiFi.RSSI(i) <= best_rssi) continue;
    best_rssi = WiFi.RSSI(i);
    best = i;
  }
  if (best < 0) {
    Serial.println("wifi  : SSID not in scan");
    WiFi.scanDelete();
    WiFi.begin(ssid, pass);
    return false;
  }

  uint8_t bssid[6];
  memcpy(bssid, WiFi.BSSID(best), sizeof(bssid));
  const int ch = WiFi.channel(best);
  Serial.printf("wifi  : binding to %02X:%02X:%02X:%02X:%02X:%02X ch %d rssi %d\n",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], ch,
                (int)best_rssi);
  WiFi.scanDelete();
  WiFi.begin(ssid, pass, ch, bssid, true);
  return true;
}

// Fallback access point. Named after the hostname so several of these on one
// desk stay distinguishable, and left open — it exists precisely because the
// owner cannot get in, so a password on it would be a locked door with the key
// inside.
void start_ap() {
  if (config::ap_mode()) return;
  char ap[40];
  snprintf(ap, sizeof(ap), "%s-setup", config::get_str("mdns_name"));
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(ap)) {
    config::set_ap_mode(true);
    Serial.printf("wifi  : could not join, hosting \"%s\" at %s\n", ap,
                  WiFi.softAPIP().toString().c_str());
  }
}

bool authorised() {
  if (!s_server.hasHeader("Authorization")) return false;
  String got = s_server.header("Authorization");
  got.trim();
  return got == String("Bearer ") + config::get_str("token");
}

void handle_health() {
  char body[192];
  snprintf(body, sizeof(body),
           "{\"uptime\":%lu,\"rssi\":%d,\"mood\":\"%s\",\"energy\":%u,"
           "\"heap\":%lu}",
           (unsigned long)(millis() / 1000), WiFi.RSSI(),
           mood::name(mood::current()), mood::energy(),
           (unsigned long)ESP.getFreeHeap());
  s_server.send(200, "application/json", body);
}

// Minimal JSON string escaping. Labels and hints are ours, not user input, but
// an unescaped quote would silently break the whole page.
void json_escape(String &out, const char *in) {
  for (const char *p = in; *p; p++) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
}

// The config document is generated from the field table, so the page never
// needs to know what settings exist — add a row in config.cpp and it appears.
String config_json() {
  String j;
  j.reserve(4096);
  j += "{\"device\":{";
  j += "\"ip\":\""; j += WiFi.localIP().toString(); j += "\",";
  j += "\"rssi\":"; j += WiFi.RSSI(); j += ",";
  j += "\"uptime_s\":"; j += (uint32_t)(millis() / 1000); j += ",";
  j += "\"mood\":\""; j += mood::name(mood::current()); j += "\",";
  j += "\"style\":\""; j += face::style_name(); j += "\",";
  j += "\"energy\":"; j += mood::energy(); j += ",";
  j += "\"heap\":"; j += (uint32_t)ESP.getFreeHeap(); j += ",";
  // The page builds its mood-test buttons from this, so adding a mood to the
  // enum is all it takes for one to appear there.
  j += "\"moods\":[";
  for (int m = 0; m <= (int)mood::Mood::Limit; m++) {
    if (m) j += ",";
    j += "\""; j += mood::name((mood::Mood)m); j += "\"";
  }
  j += "],";
  j += "\"view\":\""; j += views::name(); j += "\",";
  j += "\"clock_ok\":"; j += usage::clock_valid() ? "true" : "false"; j += ",";
  j += "\"window_remaining_s\":"; j += usage::remaining_s(); j += ",";
  j += "\"window_reset\":"; j += (uint32_t)usage::window_reset_at(); j += ",";
  j += "\"prompts\":"; j += usage::prompts(); j += ",";
  j += "\"limit_pct\":"; j += usage::percent(); j += ",";
  j += "\"limit_age_s\":"; j += usage::host_age_s(); j += ",";
  j += "\"limit_from_host\":"; j += usage::host_data() ? "true" : "false"; j += ",";
  j += "\"ap_mode\":"; j += config::ap_mode() ? "true" : "false"; j += ",";
  j += "\"version\":\"1.0\"}";

  j += ",\"groups\":[";
  const config::Field *f = config::fields();
  const size_t n = config::field_count();
  const char *current = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (current == nullptr || strcmp(current, f[i].group) != 0) {
      if (current != nullptr) j += "]},";
      current = f[i].group;
      j += "{\"name\":\""; json_escape(j, f[i].group); j += "\",\"fields\":[";
    } else {
      j += ",";
    }
    j += "{\"key\":\""; j += f[i].key;
    j += "\",\"label\":\""; json_escape(j, f[i].label);
    j += "\",\"type\":\"";
    switch (f[i].type) {
      case config::Type::Bool:     j += "bool"; break;
      case config::Type::Enum:     j += "enum"; break;
      case config::Type::Text:     j += "text"; break;
      case config::Type::Password: j += "password"; break;
      default:                     j += "int"; break;
    }
    j += "\",";
    if (f[i].type == config::Type::Text) {
      j += "\"value\":\""; json_escape(j, config::get_str(f[i].key)); j += "\"";
      j += ",\"maxlen\":"; j += f[i].max;
    } else if (f[i].type == config::Type::Password) {
      // Never send a stored secret back to the browser. Blank means unchanged.
      j += "\"value\":\"\",\"maxlen\":"; j += f[i].max;
    } else {
      j += "\"value\":"; j += config::get(f[i].key);
    }
    if (f[i].type == config::Type::Int) {
      j += ",\"min\":"; j += f[i].min;
      j += ",\"max\":"; j += f[i].max;
    }
    if (f[i].hint) { j += ",\"hint\":\""; json_escape(j, f[i].hint); j += "\""; }
    if (f[i].options) {
      j += ",\"options\":[\"";
      for (const char *p = f[i].options; *p; p++) {
        if (*p == ',') j += "\",\""; else j += *p;
      }
      j += "\"]";
    }
    j += "}";
  }
  if (current != nullptr) j += "]}";
  j += "]}";
  return j;
}

void handle_get_config() {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "application/json", config_json());
}

// Which fields are strings, so the handler knows whether to parse an integer.
config::Type type_of(const char *key) {
  const config::Field *f = config::fields();
  for (size_t i = 0; i < config::field_count(); i++) {
    if (strcmp(f[i].key, key) == 0) return f[i].type;
  }
  return config::Type::Int;
}

void handle_post_config() {
  int applied = 0, rejected = 0;
  bool network_changed = false;

  for (int i = 0; i < s_server.args(); i++) {
    const String key = s_server.argName(i);
    if (key == "plain") continue;  // WebServer's raw-body pseudo-argument

    const config::Type t = type_of(key.c_str());
    bool ok;
    if (t == config::Type::Text || t == config::Type::Password) {
      ok = config::set_str(key.c_str(), s_server.arg(i).c_str());
      if (ok && !s_server.arg(i).isEmpty() &&
          (key == "wifi_ssid" || key == "wifi_pass" || key == "mdns_name")) {
        network_changed = true;
      }
    } else {
      ok = config::set(key.c_str(), s_server.arg(i).toInt());
    }
    if (ok) applied++; else rejected++;
  }

  if (network_changed) {
    // Reassociating in place is fiddly and half-works; a restart is one line
    // and always correct. Delayed so this response actually reaches the browser.
    s_reboot_pending = true;
    s_reboot_at = millis() + 1200;
  }
  // Push the changes into the parts of the firmware that cache them.
  face::refresh();
  bell::reconfigure();

  char body[96];
  snprintf(body, sizeof(body), "%d applied, %d rejected%s\n", applied, rejected,
           network_changed ? ", rebooting" : "");
  s_server.send(rejected ? 400 : 200, "text/plain", body);
}

void handle_post_test() {
  if (s_server.hasArg("what")) {
    const String what = s_server.arg("what");
    if (what == "buzz") bell::celebrate();
    else if (what == "strike") bell::strike();
  }
  if (s_server.hasArg("mood")) {
    const int m = s_server.arg("mood").toInt();
    if (m >= 0 && m <= (int)mood::Mood::Limit) {
      mood::force((mood::Mood)m, /*sticky=*/true);
    }
  }
  s_server.send(200, "text/plain", "ok\n");
}

void handle_post_reset() {
  config::reset_defaults();
  face::refresh();
  bell::reconfigure();
  s_server.send(200, "text/plain", "defaults restored\n");
}

void install_routes() {
  // Only Authorization is captured; the core server drops other headers.
  const char *wanted[] = {"Authorization"};
  s_server.collectHeaders(wanted, 1);

  for (const EventRoute &r : kRoutes) {
    const mood::Event e = r.event;
    s_server.on(r.path, HTTP_POST, [e]() {
      if (!authorised()) {
        s_server.send(401, "text/plain", "bad token\n");
        return;
      }
      mood::post(e);
      // 200 with a token body rather than a bodyless 204: the Arduino
      // WebServer logs a warning on every zero-length response, and a serial
      // log full of noise costs more than the extra three bytes save. The
      // hooks are fire-and-forget and never read this.
      s_server.send(200, "text/plain", "ok\n");
    });
  }

  // Real rate-limit figures, forwarded by the host's statusline script. Query
  // parameters rather than a body, so the sender stays a bare curl invocation.
  s_server.on("/limits", HTTP_POST, []() {
    if (!authorised()) {
      s_server.send(401, "text/plain", "bad token\n");
      return;
    }
    if (!s_server.hasArg("pct")) {
      s_server.send(400, "text/plain", "need pct\n");
      return;
    }
    usage::note_limits(s_server.arg("pct").toInt(),
                       (time_t)s_server.arg("reset").toInt());
    s_server.send(200, "text/plain", "ok\n");
  });

  s_server.on("/health", HTTP_GET, handle_health);

  // Settings page and its API. Deliberately unauthenticated: it is LAN-only,
  // the token protects the event routes that a hook uses, and demanding a
  // password to change a buzzer tone would defeat the point of the page.
  s_server.on("/api/config", HTTP_GET, handle_get_config);
  s_server.on("/api/config", HTTP_POST, handle_post_config);
  s_server.on("/api/test", HTTP_POST, handle_post_test);
  s_server.on("/api/reset", HTTP_POST, handle_post_reset);

#ifdef HAVE_CONFIG_PAGE
  s_server.on("/", HTTP_GET, []() {
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send_P(200, "text/html", kConfigPage);
  });
#else
  s_server.on("/", HTTP_GET, []() {
    s_server.send(200, "text/plain", "settings page not built in\n");
  });
#endif

  // A typo in a hook config should be loud, not silently swallowed.
  s_server.onNotFound([]() {
    s_server.send(404, "text/plain", "unknown event\n");
  });
}

}  // namespace

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // always USB-powered; latency matters, milliamps do not
  WiFi.setAutoReconnect(true);

  // Default is a fast scan that associates with the first matching BSSID it
  // finds. On a mesh or with a repeater that is often not the nearest node —
  // scan every channel and take the strongest signal instead.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  Serial.printf("wifi  : connecting to %s\n", config::get_str("wifi_ssid"));
  connect_bound();

  install_routes();
  s_server.begin();
  s_next_retry = millis() + WIFI_RETRY_MS;
}

void tick() {
  s_server.handleClient();

  if (s_reboot_pending && millis() >= s_reboot_at) ESP.restart();

  const bool up = WiFi.status() == WL_CONNECTED;

  if (up != s_was_connected) {
    s_was_connected = up;
    if (up) {
      s_attempts = 0;
      s_backoff_ms = WIFI_RETRY_MS;
      strncpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip) - 1);
      Serial.printf("wifi  : up, %s  rssi %d  ch %d\n", s_ip, WiFi.RSSI(),
                    WiFi.channel());
      if (config::ap_mode()) {
        // Joined after all; drop the fallback AP so there is one network.
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        config::set_ap_mode(false);
      }
      if (MDNS.begin(config::get_str("mdns_name"))) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mdns  : http://%s.local/ (macOS; Windows needs the IP)\n",
                      config::get_str("mdns_name"));
      }
      Serial.printf("setup : http://%s/\n", s_ip);
    } else {
      strncpy(s_ip, "0.0.0.0", sizeof(s_ip) - 1);
      Serial.println("wifi  : lost");
    }
  }

  if (up || millis() < s_next_retry) return;

  // Supervised retry. setAutoReconnect() stops trying after the driver has
  // failed enough times — most often when the AP was simply not there at boot —
  // and without this the board sits offline forever with nothing in the log.
  s_next_retry = millis() + s_backoff_ms;
  s_attempts++;
  Serial.printf("wifi  : retry %u (status %d, next in %lus)\n", s_attempts,
                WiFi.status(), (unsigned long)(s_backoff_ms / 1000));
  s_backoff_ms = min<uint32_t>(s_backoff_ms * 2, WIFI_RETRY_MAX_MS);
  if (s_attempts >= kAttemptsBeforeAp) start_ap();
  WiFi.disconnect();
  connect_bound();
}

const char *auth_name(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-enterprise";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "?";
  }
}

void scan() {
  // Drop any association attempt first: scanning while the driver is mid-
  // handshake returns nothing, which looks like "no networks" and is not.
  WiFi.disconnect(true);
  delay(200);

  Serial.println("wifi  : scanning 2.4 GHz...");
  const int n = WiFi.scanNetworks();
  if (n <= 0) Serial.println("  nothing found");

  bool saw_target = false;
  for (int i = 0; i < n; i++) {
    const bool mine = WiFi.SSID(i) == String(WIFI_SSID);
    if (mine) saw_target = true;
    Serial.printf("  %c %-28s rssi %4d  ch %2d  %s\n", mine ? '*' : ' ',
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  auth_name(WiFi.encryptionType(i)));
  }
  if (!saw_target) {
    Serial.printf("  target SSID not visible — the ESP32-S3 has no 5 GHz radio\n");
  }

  WiFi.scanDelete();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  // A manual scan is a deliberate "try again now", so reset the backoff.
  s_backoff_ms = WIFI_RETRY_MS;
  s_next_retry = millis() + s_backoff_ms;
}

void probe() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("probe : wifi down");
    return;
  }
  Serial.printf("probe : own MAC %s  ip %s  gw %s\n", WiFi.macAddress().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());

  struct Target { const char *host; uint16_t port; const char *what; };
  const Target targets[] = {
      {nullptr,    80,         "gateway:80"},
      {"1.1.1.1",  80,         "internet 1.1.1.1:80"},
      {PROBE_HOST, PROBE_PORT, "this Mac"},
  };

  // Reading the three together is what makes this useful. Internet reachable
  // but the Mac not means the LAN path is being blocked, not that the board is
  // offline — which is the ambiguity that costs the most time to chase.
  for (const Target &t : targets) {
    IPAddress ip;
    if (t.host == nullptr) {
      ip = WiFi.gatewayIP();
    } else {
      ip.fromString(t.host);
    }
    WiFiClient c;
    const uint32_t t0 = millis();
    const bool ok = c.connect(ip, t.port, 3000);
    Serial.printf("probe : %-22s %-9s %s (%lu ms)\n", t.what,
                  ip.toString().c_str(), ok ? "reachable" : "NO ROUTE",
                  (unsigned long)(millis() - t0));
    c.stop();
  }
}

bool connected() { return s_was_connected; }
const char *ip() { return s_ip; }

}  // namespace net
