// What the screen should show, given the link state and whether a price payload
// has ever been received.
//
// This exists because the display has two things that want to paint it — the
// connection status and the price table — and a status change must never hide
// prices that have already arrived. The rule lives here, separate from the
// drawing code, so that invariant is testable without hardware.

#pragma once

#include <stdint.h>

namespace display {

enum class Link : uint8_t {
    Connecting, // no Wi-Fi yet
    WifiUp,     // Wi-Fi associated, MQTT not up
    MqttUp,     // subscribing / subscribed, live
    Lost,       // was connected, Wi-Fi dropped
};

// Short status line for the header bar.
// (Written as a single expression because the Arduino core compiles with
// -std=gnu++11, where a constexpr body may only contain one return.)
constexpr const char *statusText(Link link) {
    return link == Link::MqttUp     ? "MQTT connected, waiting"
           : link == Link::WifiUp   ? "WIFI Connected.."
           : link == Link::Lost     ? "WIFI Disconnected.."
                                    : "Connecting to Wi-Fi..";
}

struct View {
    bool showTable;      // draw the price rows
    bool barIsStatus;    // header bar carries the link status instead of the
                         // payload timestamp (i.e. the data on screen is stale)
};

// Chooses the view for the current state.
//
// The key rule: once a payload has been received, it stays on screen across link
// transitions. A reconnect or a disconnect only changes the header bar, so the
// last known prices remain visible instead of being replaced by a status
// message. Before any payload has arrived there is nothing to show but status.
constexpr View chooseView(Link link, bool haveBoard) {
    return haveBoard ? View{true, link != Link::MqttUp} : View{false, false};
}

}  // namespace display
