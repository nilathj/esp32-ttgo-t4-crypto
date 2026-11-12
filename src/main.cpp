#include <Arduino.h>
#include <ArduinoJson.h>
#include <PsychicMqttClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Free_Fonts.h"
#include "NotoSansBold15.h"
#include "quote_board.h"
#include "quote_layout.h"

#define AA_FONT_SMALL NotoSansBold15
#define WIFI_SSID "xxxxx"
#define WIFI_PASSWORD "xxxxx"

#define MQTT_HOST IPAddress(192, 168, 0, 215)
#define MQTT_PORT 1883
#define MQTT_PRICE_TOPIC "crypto/prices"

// A single status message larger than this is dropped instead of being parsed.
// Messages longer than the MQTT receive buffer (1 KB) are still accepted: the
// MQTT client reassembles the multipart message before calling us. This is only
// a sanity limit, sized to the board, not a chunk size.
static constexpr size_t kMaxJsonBytes = 64 * 1024;

TFT_eSPI tft = TFT_eSPI();
PsychicMqttClient mqttClient;
QueueHandle_t boardQueue = nullptr;

static char mqttUri[64];
static std::atomic<bool> wifiConnected{false};
static bool mqttStarted = false;

// Draws the coloured status bar with a single line of text.
void drawStatus(const char *txt) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, tft.width(), quotes::kStatusHeight, TFT_GREENYELLOW);
    tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(txt, tft.width() / 2, 2);
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
void drawQuoteBoard(const quotes::Board &board) {
    const int width = tft.width();
    const int height = tft.height();
    const quotes::Layout layout = layoutFor(board);
    tft.fillScreen(TFT_BLACK);

    // Timestamp bar.
    tft.fillRect(0, 0, width, quotes::kStatusHeight, TFT_GREENYELLOW);
    if (board.time[0] != '\0') {
        tft.setTextColor(TFT_BLACK, TFT_GREENYELLOW);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(board.time, width / 2, 2);
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

void connectToWifi() {
    Serial.println("Connecting to Wi-Fi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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
    connectToWifi();
}

void loop() {
    // Link status is drawn here, in the same task that owns the display.
    static uint8_t lastLink = 0xff;
    static bool everConnected = false;
    const bool mqttUp = mqttClient.connected();
    const bool wifiUp = wifiConnected.load();
    if (wifiUp)
        everConnected = true;

    // 0 = connecting, 1 = Wi-Fi up, 2 = MQTT up, 3 = Wi-Fi lost
    const uint8_t link = mqttUp ? 2 : (wifiUp ? 1 : (everConnected ? 3 : 0));
    if (link != lastLink) {
        lastLink = link;
        switch (link) {
        case 2:
            drawStatus("MQTT Connected..");
            break;
        case 1:
            drawStatus("WIFI Connected..");
            break;
        case 3:
            drawStatus("WIFI Disconnected..");
            break;
        default:
            drawStatus("Connecting to Wi-Fi..");
            break;
        }
    }

    if (wifiUp && !mqttStarted) {
        mqttStarted = true;
        mqttClient.connect();
    }

    if (boardQueue != nullptr) {
        quotes::Board board;
        if (xQueueReceive(boardQueue, &board, pdMS_TO_TICKS(100)) == pdPASS)
            drawQuoteBoard(board);
    } else {
        delay(100);
    }
}
