#include <Arduino.h>
#include <ArduinoJson.h>
#include <PsychicMqttClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <atomic>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <strings.h>

#include "Free_Fonts.h"
#include "NotoSansBold15.h"
#include "display_state.h"
#include "quote_board.h"
#include "quote_layout.h"
#include "wifi_setup.h"

#define AA_FONT_SMALL NotoSansBold15

#define MQTT_HOST IPAddress(192, 168, 0, 215)
#define MQTT_PORT 1883
#define MQTT_PRICE_TOPIC "crypto/prices"

// The setup portal closes after this long, so a board that nobody provisions
// does not sit there advertising an access point indefinitely.
static constexpr unsigned long kPortalTimeoutSeconds = 300;

// How long to try the stored network before offering the setup portal.
static constexpr unsigned long kConnectTimeoutSeconds = 30;

// A single status message larger than this is dropped instead of being parsed.
// Messages longer than the MQTT receive buffer (1 KB) are still accepted: the
// MQTT client reassembles the multipart message before calling us. This is only
// a sanity limit, sized to the board, not a chunk size.
static constexpr size_t kMaxJsonBytes = 64 * 1024;

TFT_eSPI tft = TFT_eSPI();
PsychicMqttClient mqttClient;
WiFiManager wifiManager;
QueueHandle_t boardQueue = nullptr;

static char mqttUri[64];
static std::atomic<bool> wifiConnected{false};
static bool mqttStarted = false;

// The last payload received, kept so the table can be repainted after a link
// transition instead of being replaced by a status message. The server publishes
// with retain set, so a payload normally arrives the moment we subscribe.
static quotes::Board lastBoard;
static bool haveBoard = false;
static display::Link shownLink = display::Link::Connecting;
static bool linkDrawn = false;

// Setup access point. The name is derived from the chip so several boards in
// range can be told apart; the password is generated per boot and is never
// compiled in. Credentials for the real network are never stored here: the
// Wi-Fi stack keeps them in NVS (see the README).
static char portalSsid[32];
static char portalPassword[wifi_setup::kPasswordBufferSize];

// Draws the coloured status bar with a single line of text.
void drawStatus(const char *txt) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, tft.width(), quotes::kStatusHeight, TFT_GREENYELLOW);
    tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(txt, tft.width() / 2, 2);
}
// Explains how to reach the setup portal, including the per-boot password.
// Called from the portal callback, which runs on the Arduino task in blocking
// mode, so touching the display here stays on the task that owns it.
void drawProvisioningScreen() {
    const int width = tft.width();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, width, quotes::kStatusHeight, TFT_GREENYELLOW);
    tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Wi-Fi setup", width / 2, 2);

    const int left = quotes::kLayoutMargin;
    const int lineHeight = tft.fontHeight(GFXFF) + 8;
    int y = quotes::kStatusHeight + quotes::kHeaderPadTop + 6;

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Join network:", left, y);
    y += lineHeight;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(portalSsid, left, y);
    y += lineHeight;
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Password:", left, y);
    y += lineHeight;
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(portalPassword, left, y);
    y += lineHeight + 4;
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Then open 192.168.4.1", left, y);
}

// Called by WiFiManager when the setup portal actually starts.
void onPortalStarted(WiFiManager *) {
    Serial.println("Wi-Fi setup portal started");
    Serial.printf("  network:  %s\n", portalSsid);
    // This is the one-time portal password, shown so it can be joined; the
    // network passphrase the user types into the portal is never logged.
    Serial.printf("  password: %s\n", portalPassword);
    Serial.println("  open http://192.168.4.1 to configure");
    drawProvisioningScreen();
}


// ---- Price table ----------------------------------------------------------

// Shading on every other row, so the eye can follow one coin across the table.
static const uint16_t kRowShade = 0x18E3; // RGB565 dark grey

// Widest rendering of one field across all coins, in the loaded font.
template <typename GetField>
static int widestField(const quotes::Board &board, GetField getField) {
    int width = 0;
    for (size_t i = 0; i < board.coinCount; i++) {
        const int fieldWidth = tft.textWidth(getField(board.coins[i]), GFXFF);
        if (fieldWidth > width)
            width = fieldWidth;
    }
    return width;
}

// Shortens `src` so it fits in `maxWidth` pixels of the loaded font, ending in
// ".." when cut.
static void fitText(char *dst, size_t dstSize, const char *src, int maxWidth) {
    quotes::fitTextToWidth(
        dst, dstSize, src, maxWidth,
        [](const char *text) { return tft.textWidth(text, GFXFF); });
}

// A column must be wide enough for both its label and its widest value, so the
// header row lines up with the data instead of being dropped.
template <typename GetField>
static int columnWidth(const quotes::Board &board, const char *label,
                       GetField getField) {
    const int labelWidth = tft.textWidth(label, GFXFF);
    const int valueWidth = widestField(board, getField);
    return valueWidth > labelWidth ? valueWidth : labelWidth;
}

// Column widths for the current payload, fitted to the screen. Computed per
// message, so the layout adapts to the data instead of relying on fixed pixel
// offsets. Because each column is at least as wide as its label, the header row
// always lines up with the data.
static quotes::Layout layoutFor(const quotes::Board &board) {
    return quotes::computeLayout(
        tft.width(),
        columnWidth(board, "COIN", [](const quotes::Coin &c) { return c.name; }),
        columnWidth(board, "PRICE", [](const quotes::Coin &c) { return c.price; }),
        columnWidth(board, "24H", [](const quotes::Coin &c) { return c.change; }),
        columnWidth(board, "VOL", [](const quotes::Coin &c) { return c.volume; }));
}

// Draws a parsed snapshot. Only called from loop(), never from a network
// callback, because the display writes take far longer than an MQTT callback
// is allowed to block.
//
// `staleText` non-null means the connection is not live, so the header bar
// carries that text (in a warning colour) instead of the payload timestamp. The
// rows are always drawn, so prices are never hidden by a link transition.
void drawQuoteBoard(const quotes::Board &board, const char *staleText) {
    const int width = tft.width();
    const int height = tft.height();
    const quotes::Layout layout = layoutFor(board);
    tft.fillScreen(TFT_BLACK);

    // Header bar: payload timestamp when live, link status when stale.
    const bool stale = staleText != nullptr && staleText[0] != '\0';
    const uint16_t barBg = stale ? TFT_ORANGE : TFT_GREENYELLOW;
    const char *barText = stale ? staleText : board.time;
    tft.fillRect(0, 0, width, quotes::kStatusHeight, barBg);
    if (barText[0] != '\0') {
        tft.setTextColor(TFT_BLACK, barBg);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(barText, width / 2, 2);
    }

    // Column labels, in the same columns as the data below them. They sit in
    // their own band, clear of the timestamp bar above and the divider below.
    const int textHeight = tft.fontHeight(GFXFF);
    const int headerCentre = quotes::headerCentreY(textHeight);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("PRICE", layout.priceRight, headerCentre, GFXFF);
    tft.drawString("24H", layout.changeRight, headerCentre, GFXFF);
    if (layout.showVolume)
        tft.drawString("VOL", layout.volumeRight, headerCentre, GFXFF);
    // The name column can be squeezed in a tight layout, so its label is the
    // only one that may not fit.
    if (layout.nameWidth >= tft.textWidth("COIN", GFXFF)) {
        tft.setTextDatum(ML_DATUM);
        tft.drawString("COIN", layout.nameX, headerCentre, GFXFF);
    }
    tft.drawFastHLine(0, quotes::headerDividerY(textHeight), width,
                      TFT_DARKGREY);

    const int rowHeight = textHeight + quotes::kRowGap;
    const int rowsTop = quotes::rowsTopY(textHeight);
    const int rows =
        quotes::rowCapacity(height, rowsTop, rowHeight, quotes::kBottomMargin);

    // Payload order is the display order: rows are never re-sorted here.
    int y = rowsTop;
    for (size_t i = 0; i < board.coinCount && static_cast<int>(i) < rows; i++) {
        const quotes::Coin &coin = board.coins[i];
        const uint16_t background = (i % 2) ? kRowShade : TFT_BLACK;
        tft.fillRect(0, y, width, rowHeight, background);
        const int centre = y + rowHeight / 2;

        char name[sizeof(coin.name)];
        fitText(name, sizeof(name), coin.name, layout.nameWidth);

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(TFT_WHITE, background);
        tft.drawString(name, layout.nameX, centre, GFXFF);

        tft.setTextDatum(MR_DATUM);
        tft.drawString(coin.price, layout.priceRight, centre, GFXFF);

        const bool falling = coin.change[0] == '-';
        tft.setTextColor(falling ? TFT_RED : TFT_GREEN, background);
        tft.drawString(coin.change, layout.changeRight, centre, GFXFF);

        if (layout.showVolume) {
            tft.setTextColor(TFT_LIGHTGREY, background);
            tft.drawString(coin.volume, layout.volumeRight, centre, GFXFF);
        }

        y += rowHeight;
    }

    if (board.coinCount == 0) {
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No prices in payload", width / 2, rowsTop + rowHeight,
                       GFXFF);
    }
}

// Serial console: typing "reset" erases the saved network so the next boot
// offers the setup portal again. This is the way to move the board to another
// network without erasing the whole flash.
void pollSerialCommands() {
    static char line[24];
    static size_t length = 0;

    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0)
            break;
        if (c == '\r' || c == '\n') {
            line[length] = '\0';
            if (length > 0 && strcasecmp(line, "reset") == 0) {
                Serial.println("Erasing saved Wi-Fi credentials; restarting");
                wifiManager.resetSettings();
                delay(200);
                ESP.restart();
            }
            length = 0;
        } else if (length + 1 < sizeof(line)) {
            line[length++] = static_cast<char>(c);
        } else {
            length = 0; // overlong input: drop the line
        }
    }
}

// Single entry point for painting the screen. Both the price table and the link
// status go through here, so the two can never fight over the display: once a
// payload has arrived it stays on screen, and a link transition only changes the
// header bar. See include/display_state.h for the rule.
void renderView(display::Link link) {
    const display::View view = display::chooseView(link, haveBoard);
    if (!view.showTable) {
        drawStatus(display::statusText(link));
        return;
    }
    drawQuoteBoard(lastBoard,
                   view.barIsStatus ? display::statusText(link) : nullptr);
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.print("WiFi connected, IP address: ");
        Serial.println(WiFi.localIP());
        wifiConnected.store(true);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        wifiConnected.store(false);
        break;
    default:
        break;
    }
}

void onMqttConnect(bool sessionPresent) {
    Serial.println("Connected to MQTT.");
    Serial.print("Session present: ");
    Serial.println(sessionPresent);
}

void onMqttDisconnect(bool sessionPresent) {
    Serial.println("Disconnected from MQTT.");
}

void onMqttError(esp_mqtt_error_codes_t error) {
    Serial.printf("MQTT error: type %d, esp-tls 0x%x, tls-stack 0x%x, socket "
                  "errno %d\n",
                  error.error_type, error.esp_tls_last_esp_err,
                  error.esp_tls_stack_err, error.esp_transport_sock_errno);
}

// Runs in the MQTT client task. Keep it short and non-blocking: parse the
// payload into a small snapshot and hand that to the Arduino loop task, which
// owns the display.
void onMqttMessage(char *topic, char *payload, int retain, int qos, bool dup) {
    (void)topic;
    (void)retain;
    (void)qos;
    (void)dup;

    if (payload == nullptr || boardQueue == nullptr)
        return;

    const size_t len = strnlen(payload, kMaxJsonBytes + 1);
    if (len == 0) {
        Serial.println("Ignoring empty MQTT payload");
        return;
    }
    if (len > kMaxJsonBytes) {
        Serial.printf("Ignoring MQTT payload larger than %u bytes\n",
                      static_cast<unsigned>(kMaxJsonBytes));
        return;
    }

    quotes::Board board;
    DeserializationError error = quotes::parseBoard(payload, len, board);
    if (error) {
        Serial.printf("deserializeJson() failed: %s\n", error.c_str());
        return;
    }

    if (xQueueOverwrite(boardQueue, &board) != pdPASS)
        Serial.println("Failed to queue the parsed payload");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 0);
    tft.loadFont(AA_FONT_SMALL);

    // Single-slot queue: if a new message arrives before the previous one was
    // drawn, the newest snapshot simply replaces it.
    boardQueue = xQueueCreate(1, sizeof(quotes::Board));

    const IPAddress mqttHost = MQTT_HOST;
    snprintf(mqttUri, sizeof(mqttUri), "mqtt://%s:%d",
             mqttHost.toString().c_str(), MQTT_PORT);

    // PsychicMqttClient is built on the ESP-IDF MQTT client: it reassembles
    // multipart messages of any size before invoking onTopic(), so payloads
    // larger than the 1 KB receive buffer are delivered complete.
    mqttClient.setServer(mqttUri);
    mqttClient.setClientId("ttgo-t4-crypto");
    mqttClient.setKeepAlive(30);
    mqttClient.setAutoReconnect(true);
    // The message callback parses JSON, so give the MQTT task some headroom.
    mqttClient.setTaskStackAndPriority(8192, 5);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onError(onMqttError);
    // onTopic() subscribes now and again after every reconnect.
    mqttClient.onTopic(MQTT_PRICE_TOPIC, 1, onMqttMessage);

    WiFi.onEvent(WiFiEvent);
    WiFi.setAutoReconnect(true);

    // ---- Wi-Fi provisioning ----
    // Credentials are entered once through a captive portal and saved by the
    // ESP32 Wi-Fi stack in NVS, so nothing about the network is compiled into
    // the firmware or committed to this repository.
    wifi_setup::makePassword(portalPassword, sizeof(portalPassword),
                             []() { return esp_random(); });
    if (!wifi_setup::isAcceptablePassword(portalPassword)) {
        // Fail closed: an access point with a password WPA2 rejects is an open
        // access point, so never advertise one.
        Serial.println("Setup password rejected; refusing to open the portal");
        drawStatus("Setup password error");
        return;
    }

    const uint64_t chipId = ESP.getEfuseMac();
    snprintf(portalSsid, sizeof(portalSsid), "TTGO-Crypto-%02X%02X",
             static_cast<unsigned>((chipId >> 8) & 0xFF),
             static_cast<unsigned>(chipId & 0xFF));

    wifiManager.setAPCallback(onPortalStarted);
    wifiManager.setConfigPortalTimeout(kPortalTimeoutSeconds);
    wifiManager.setConnectTimeout(kConnectTimeoutSeconds);
    wifiManager.setMinimumSignalQuality(10);
    wifiManager.setHostname("ttgo-t4-crypto");
    wifiManager.setTitle("TTGO T4 crypto prices");
    // WiFiManager logs its arguments at the verbose/dev levels, which can
    // include the passphrase; keep its serial output off.
    wifiManager.setDebugOutput(false);

    drawStatus("Connecting to Wi-Fi..");
    // Connects with the saved network if there is one, otherwise (or if that
    // fails) raises the setup portal and blocks until it is done or times out.
    if (!wifiManager.autoConnect(portalSsid, portalPassword)) {
        Serial.println("Wi-Fi not configured; restarting to offer setup again");
        drawStatus("Wi-Fi setup timed out");
        delay(2000);
        ESP.restart();
    }

    // autoConnect() returning true means we are associated. Set the flag here as
    // well, so the loop cannot miss a GOT_IP event that was raised while the
    // stack was coming up.
    if (WiFi.status() == WL_CONNECTED)
        wifiConnected.store(true);
}

void loop() {
    pollSerialCommands();

    // Read the payload first, so a message that arrives in the same pass as a
    // link change is drawn by the repaint below rather than being overwritten
    // by it.
    bool boardUpdated = false;
    if (boardQueue != nullptr) {
        quotes::Board board;
        if (xQueueReceive(boardQueue, &board, pdMS_TO_TICKS(100)) == pdPASS) {
            lastBoard = board;
            haveBoard = true;
            boardUpdated = true;
        }
    } else {
        delay(100);
    }

    const bool mqttUp = mqttClient.connected();
    const bool wifiUp = wifiConnected.load();
    static bool everConnected = false;
    if (wifiUp)
        everConnected = true;

    display::Link link = display::Link::Connecting;
    if (mqttUp)
        link = display::Link::MqttUp;
    else if (wifiUp)
        link = display::Link::WifiUp;
    else if (everConnected)
        link = display::Link::Lost;

    const bool linkChanged = !linkDrawn || link != shownLink;
    shownLink = link;
    linkDrawn = true;

    // One repaint decision for both triggers, so the status can never paint
    // over data that has already arrived.
    if (boardUpdated || linkChanged)
        renderView(link);

    if (wifiUp && !mqttStarted) {
        mqttStarted = true;
        mqttClient.connect();
    }
}
