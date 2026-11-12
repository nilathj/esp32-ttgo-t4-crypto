// Tests for the display view rule.
//
// The bug these cover: the retained price payload arrives immediately on
// subscribe and was drawn, then the "MQTT Connected.." status transition on the
// next loop pass painted over it — so nothing reappeared until the server's
// 5-minute cron published again.
//
// Run with: pio test -e native

#include <string.h>

#include <unity.h>

#include "display_state.h"

using display::chooseView;
using display::Link;

// The regression itself: with prices already on screen, going live must keep
// them on screen. No status repaint may replace the table.
void testLiveWithDataShowsTable() {
    const display::View view = chooseView(Link::MqttUp, true);
    TEST_ASSERT_TRUE(view.showTable);
    TEST_ASSERT_FALSE(view.barIsStatus);  // bar keeps the payload timestamp
}

// Losing Wi-Fi after data arrived must not blank the prices: the rows stay and
// only the bar switches to the status line, so the data is marked stale rather
// than erased.
void testDisconnectKeepsLastPricesVisible() {
    const display::View view = chooseView(Link::Lost, true);
    TEST_ASSERT_TRUE(view.showTable);
    TEST_ASSERT_TRUE(view.barIsStatus);
}

// Reconnecting after a drop keeps the previous table too.
void testReconnectKeepsLastPricesVisible() {
    TEST_ASSERT_TRUE(chooseView(Link::WifiUp, true).showTable);
    TEST_ASSERT_TRUE(chooseView(Link::Connecting, true).showTable);
}

// Before the first payload there is nothing to show but status.
void testNoDataYetShowsStatusOnly() {
    const Link links[] = {Link::Connecting, Link::WifiUp, Link::MqttUp,
                          Link::Lost};
    for (Link link : links) {
        const display::View view = chooseView(link, false);
        TEST_ASSERT_FALSE(view.showTable);
        TEST_ASSERT_FALSE(view.barIsStatus);
    }
}

// The status line exists for every state and is never empty.
void testStatusTextForEveryLinkState() {
    const Link links[] = {Link::Connecting, Link::WifiUp, Link::MqttUp,
                          Link::Lost};
    for (Link link : links) {
        const char *text = display::statusText(link);
        TEST_ASSERT_NOT_NULL(text);
        TEST_ASSERT_GREATER_THAN(0, static_cast<int>(strlen(text)));
    }
    // Before the first payload the live state should say what it is waiting for,
    // not claim success.
    TEST_ASSERT_EQUAL_STRING("MQTT connected, waiting",
                             display::statusText(Link::MqttUp));
    TEST_ASSERT_EQUAL_STRING("WIFI Disconnected..",
                             display::statusText(Link::Lost));
}

// Exhaustive: the invariant holds for every (link, haveBoard) combination.
void testTableIsNeverHiddenOnceDataExists() {
    const Link links[] = {Link::Connecting, Link::WifiUp, Link::MqttUp,
                          Link::Lost};
    for (Link link : links) {
        TEST_ASSERT_TRUE(chooseView(link, true).showTable);
        TEST_ASSERT_FALSE(chooseView(link, false).showTable);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(testLiveWithDataShowsTable);
    RUN_TEST(testDisconnectKeepsLastPricesVisible);
    RUN_TEST(testReconnectKeepsLastPricesVisible);
    RUN_TEST(testNoDataYetShowsStatusOnly);
    RUN_TEST(testStatusTextForEveryLinkState);
    RUN_TEST(testTableIsNeverHiddenOnceDataExists);
    return UNITY_END();
}
