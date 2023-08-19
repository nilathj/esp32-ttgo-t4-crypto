# MQTT Crypto price tracker client using an ESP32-TTGO-T4

The ESP32-TTGO-T4 V1.3 is a 65k colour 320x240 2.4 inch LCD that uses the ILI9341 screen driver. It has a built in ESP32 development board with support for USB/Li-Po battery.  The board also has 3 buttons for user input.  These are not used in this project.  
Text is drawn by using the TFT_eSPI library using the google Noto Sans anti-aliased font that is loaded into a flash array.

The price tracker client display the coin name, price, daily % move (+ or -), daily volume.  This information is received by the client listening in on a MQTT topic as a JSON payload.

### Crypto prices display
![Crypto prices](/docs//images/cryproPrices.png)

## Prerequisites
* This code is using platformio with TFT_eSPI, AsyncMqttClient and ArduinoJson libraries.  Remember to install these libraries first. Refer platformio.ini.
* A MQTT server publishing price JSON payloads to the MQTT_PRICE_TOPIC (crypto/prices) periodically. Refer to [Crypto price server](https://github.com/nilathj/crypto-price-server)
* Remember to set the WIFI SSID and password in main.cpp

## Crypto price display code
1. Connect to wifi
2. Connect to MQTT server
3. Subscribe to MQTT_PRICE_TOPIC
4. mqttClient.onMessage
   - Deserialise the JSON payload in the MQTT message
   - Parse JSON
   - Display crypto price data on the TFT using TFT_eSPI library.

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
