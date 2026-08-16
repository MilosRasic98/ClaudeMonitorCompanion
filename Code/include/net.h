#pragma once
#include <stdint.h>

// Wi-Fi plus the event endpoint the Claude Code hooks POST to.
//
// The API is deliberately dumb: the event type rides in the URL path and there
// is no request body. That is what lets a hook be a bare `curl` invocation in
// exec form, identical on macOS and Windows, with no shell, no script, and no
// JSON quoting anywhere.

namespace net {

void begin();

// Reconnect supervision and HTTP servicing. Call from loop().
void tick();

bool connected();
const char *ip();

// Diagnostics: list the 2.4 GHz networks the board can actually see. The
// ESP32-S3 has no 5 GHz radio, so an SSID missing here is usually the whole
// explanation for a failure to associate.
void scan();

// Outbound TCP probe from the board: the gateway, then PROBE_HOST. If outbound
// works while inbound does not, the AP is isolating its clients.
void probe();

}  // namespace net
