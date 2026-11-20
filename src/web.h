// ---------------------------------------------------------------------------
// web.h  -  HTTP control server (synchronous WebServer from the Arduino core).
// Serves the control page and a small JSON API that drives the BLE layer.
// ---------------------------------------------------------------------------
#pragma once

namespace Web {
void begin();    // register routes and start listening
void loop();     // service clients; call every loop()
}
