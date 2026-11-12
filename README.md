# MQTT Crypto price tracker client using an ESP32-TTGO-T4

The ESP32-TTGO-T4 V1.3 is a 65k colour 320x240 2.4 inch LCD that uses the ILI9341 screen driver. It has a built in ESP32 development board with support for USB/Li-Po battery.  The board also has 3 buttons for user input.  These are not used in this project.  
Text is drawn by using the TFT_eSPI library using the google Noto Sans anti-aliased font that is loaded into a flash array.

The price tracker client displays the coin name, price, daily % move (+ or -) and daily volume.  This information is received by the client listening in on a MQTT topic as a JSON payload.

### Crypto prices display
![Crypto prices](/docs//images/cryproPrices.png)

> The screenshot above predates the table rework described below.

## Display layout
The screen is portrait, 240 x 320. From the top:

1. A timestamp bar with the `time` field of the payload.
2. A header row (`COIN`, `PRICE`, `24H`, `VOL`).
3. One row per coin, in payload order (rows are never re-sorted). 12 rows fit;
   any further coins are not drawn.

Column widths are measured from the payload itself, using the font loaded at
runtime, so nothing is positioned with hand-tuned pixel offsets and longer
values cannot collide with the next column. Prices, change and volume are
right-aligned so their last digits line up; spare width is shared out between
the columns as spacing. Rows alternate a dark grey shading so one coin can be
followed across the table.

When the measured columns plus the volume column do not fit the panel, the
volume column is dropped and the remaining three columns keep their spacing.
This is decided per message, so it depends on the data: values such as
`$1,234.56` and `-12.34%` are wide enough to drop it, while `$28,451` and
`2.2%` leave room. A coin name too long for its column is shortened with `..`.

## Prerequisites
* This code is using platformio with TFT_eSPI, PsychicMqttClient and ArduinoJson 7 libraries.  Remember to install these libraries first. Refer platformio.ini.
* A MQTT server publishing price JSON payloads to the MQTT_PRICE_TOPIC (crypto/prices) periodically. Refer to [Crypto price server](https://github.com/nilathj/crypto-price-server)
* Remember to set the WIFI SSID and password in main.cpp

## Crypto price display code
1. Connect to wifi
2. Connect to MQTT server
3. Subscribe to MQTT_PRICE_TOPIC with `mqttClient.onTopic()`
4. onMqttMessage (MQTT client task)
   - Deserialise the JSON payload in the MQTT message (bounded snapshot, `include/quote_board.h`)
   - Hand the snapshot to the Arduino loop task
5. loop task
   - Display crypto price data on the TFT using TFT_eSPI library.

## Large payloads
MQTT payloads larger than the client's 1 KB receive buffer arrive in several
parts. PsychicMqttClient (built on the ESP-IDF MQTT client) reassembles the
parts and only then calls the message handler, so the JSON is never parsed from
a fragment. The handler runs on the MQTT client task, which is given an 8 KB
stack in `setup()` because it does the parsing.

The JSON is parsed with an ArduinoJson filter that keeps only `time` and the
four displayed fields of each coin, into a fixed-size snapshot of at most 16
coins (`include/quote_board.h`). Parse allocations come from PSRAM when the
board has it, falling back to internal RAM otherwise. The snapshot is handed to
the Arduino loop task through a one-slot queue, and the display is written only
from `loop()`.

Failure modes are explicit rather than fatal: a payload over 64 KB, a malformed
payload, or one that does not fit in memory is logged to serial and ignored,
leaving the previous screen up. Note that the reassembly buffer itself is
allocated by the MQTT client for the full message, so a broker that publishes
messages larger than the free heap can still exhaust the client; keep the
payload to what the display can use, or cap it on the broker
(e.g. mosquitto `message_size_limit`).

## Tests
Host tests run against the same headers the firmware compiles: the payload
parser (`test_quote_board`, including a 68 KB payload that is deliberately not
null-terminated) and the table geometry (`test_quote_layout`, including the
volume-column drop rule and the rows-per-screen calculation):

```
pio test -e native
```

## Structure of the expected JSON in the MQTT payload
```
{
   "time":"Sat 1-04-23  19:09:15",
   "coins":[
      {
         "name":"BTC",
         "price":"$28,451",
         "price_change_24h":"2.2%",
         "vol":"17B"
      },
      {
         "name":"ETH",
         "price":"$1,825",
         "price_change_24h":"1.5%",
         "vol":"10B"
      },
      {
         "name":"ADA",
         "price":"$0.39",
         "price_change_24h":"1.4%",
         "vol":"759M"
      },
      {
         "name":"DOT",
         "price":"$6.26",
         "price_change_24h":"0.6%",
         "vol":"202M"
      }
   ]
}
```

## Design
Python crypto price server running in docker --> MQTT server --> ESP32-TTGO-T4 client

## Hardware
- [TTGO T4](https://www.aliexpress.com/w/wholesale-TTGO-T4.html)
