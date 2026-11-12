// Regression tests for the payload parsing that runs on the TTGO T4.
//
// These run on the host against the same include/quote_board.h the firmware
// compiles, so the 1 KB MQTT fragmentation bug and the memory bounds are
// covered without hardware. Run with: pio test -e native

#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "quote_board.h"

namespace {

// Builds a payload with `coins` coins, optionally padded with fields the
// display never reads (the server may well send more than we show).
std::string makePayload(size_t coins, bool withExtraFields) {
    std::string json = "{\"time\":\"Sat 1-04-23  19:09:15\",\"coins\":[";
    for (size_t i = 0; i < coins; i++) {
        if (i)
            json += ",";
        json += "{\"name\":\"C" + std::to_string(i) +
                "\",\"price\":\"$1,234.56\",\"price_change_24h\":\"-2.2%\","
                "\"vol\":\"17B\"";
        if (withExtraFields)
            json += ",\"market_cap\":\"123456789\",\"chart\":\"[1,2,3,4,5,6,7,8,"
                    "9,10]\",\"description\":\"padding padding padding padding "
                    "padding padding padding padding padding padding\"";
        json += "}";
    }
    json += "]}";
    return json;
}

}  // namespace

void testReadmePayload() {
    const char *json = "{\"time\":\"Sat 1-04-23  19:09:15\",\"coins\":["
                       "{\"name\":\"BTC\",\"price\":\"$28,451\","
                       "\"price_change_24h\":\"2.2%\",\"vol\":\"17B\"},"
                       "{\"name\":\"ETH\",\"price\":\"$1,825\","
                       "\"price_change_24h\":\"-1.5%\",\"vol\":\"10B\"}]}";
    quotes::Board board;
    TEST_ASSERT_FALSE(quotes::parseBoard(json, strlen(json), board));
    TEST_ASSERT_EQUAL_STRING("Sat 1-04-23  19:09:15", board.time);
    TEST_ASSERT_EQUAL_UINT(2, board.coinCount);
    TEST_ASSERT_EQUAL_UINT(2, board.totalCoins);
    TEST_ASSERT_EQUAL_STRING("BTC", board.coins[0].name);
    TEST_ASSERT_EQUAL_STRING("$28,451", board.coins[0].price);
    TEST_ASSERT_EQUAL_STRING("2.2%", board.coins[0].change);
    TEST_ASSERT_EQUAL_STRING("17B", board.coins[0].volume);
    TEST_ASSERT_EQUAL_STRING("ETH", board.coins[1].name);
    TEST_ASSERT_EQUAL_STRING("-1.5%", board.coins[1].change);
}

// A payload far larger than the 1 KB MQTT receive buffer must be handled, and
// the buffer is deliberately NOT null terminated to prove the length is used.
void testLargePayloadWithoutTerminator() {
    const std::string json = makePayload(300, true);
    TEST_ASSERT_GREATER_THAN(64 * 1024 - 8192, json.size());

    char *buf = new char[json.size()];
    memcpy(buf, json.data(), json.size());

    quotes::Board board;
    const DeserializationError error = quotes::parseBoard(buf, json.size(), board);
    delete[] buf;

    TEST_ASSERT_FALSE(error);
    TEST_ASSERT_EQUAL_UINT(quotes::kMaxCoins, board.coinCount);
    TEST_ASSERT_EQUAL_UINT(300, board.totalCoins);
    TEST_ASSERT_EQUAL_STRING("C0", board.coins[0].name);
    TEST_ASSERT_EQUAL_STRING("-2.2%", board.coins[0].change);
    TEST_ASSERT_EQUAL_STRING("C15", board.coins[quotes::kMaxCoins - 1].name);
}

// Parsing a fragment must fail cleanly: this is what the old code did not do.
void testFragmentFailsCleanly() {
    const std::string json = makePayload(20, false);
    quotes::Board board;
    const DeserializationError error =
        quotes::parseBoard(json.data(), json.size() / 2, board);
    TEST_ASSERT_TRUE(error);
    TEST_ASSERT_EQUAL_UINT(0, board.coinCount);
}

void testMissingAndNullFields() {
    const char *json = "{\"coins\":[{\"name\":\"BTC\"},{},{\"price\":null,"
                       "\"price_change_24h\":\"\",\"vol\":\"\"}]}";
    quotes::Board board;
    TEST_ASSERT_FALSE(quotes::parseBoard(json, strlen(json), board));
    TEST_ASSERT_EQUAL_UINT(3, board.coinCount);
    TEST_ASSERT_EQUAL_STRING("", board.time);
    TEST_ASSERT_EQUAL_STRING("BTC", board.coins[0].name);
    TEST_ASSERT_EQUAL_STRING("", board.coins[0].price);
    TEST_ASSERT_EQUAL_STRING("", board.coins[0].change);
    TEST_ASSERT_EQUAL_STRING("", board.coins[1].name);
    TEST_ASSERT_EQUAL_STRING("", board.coins[2].price);
}

void testOddCoinsShapes() {
    const char *cases[] = {"{\"time\":\"t\"}",
                           "{\"time\":\"t\",\"coins\":null}",
                           "{\"time\":\"t\",\"coins\":{}}",
                           "{\"time\":\"t\",\"coins\":[\"BTC\",42,null]}"};
    for (const char *json : cases) {
        quotes::Board board;
        TEST_ASSERT_FALSE(quotes::parseBoard(json, strlen(json), board));
        TEST_ASSERT_EQUAL_UINT(0, board.coinCount);
    }
}

void testOverLongValuesAreTruncated() {
    const char *json =
        "{\"time\":\"0123456789012345678901234567890123456789\",\"coins\":["
        "{\"name\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZ\",\"price\":\"$1234567890123"
        "4567890\",\"price_change_24h\":\"+12345678901234%\",\"vol\":\"12345"
        "678901234567890\"}]}";
    quotes::Board board;
    TEST_ASSERT_FALSE(quotes::parseBoard(json, strlen(json), board));
    TEST_ASSERT_EQUAL_UINT(sizeof(board.time) - 1, strlen(board.time));
    TEST_ASSERT_EQUAL_UINT(sizeof(board.coins[0].name) - 1,
                           strlen(board.coins[0].name));
    TEST_ASSERT_EQUAL_UINT(sizeof(board.coins[0].price) - 1,
                           strlen(board.coins[0].price));
    TEST_ASSERT_EQUAL_UINT(sizeof(board.coins[0].change) - 1,
                           strlen(board.coins[0].change));
    TEST_ASSERT_EQUAL_UINT(sizeof(board.coins[0].volume) - 1,
                           strlen(board.coins[0].volume));
}

void testMalformedAndEmptyInput() {
    quotes::Board board;

    const char *bad = "{\"time\":\"x\",\"coins\":[{\"name\":\"BTC\"";
    TEST_ASSERT_TRUE(quotes::parseBoard(bad, strlen(bad), board));
    TEST_ASSERT_EQUAL_UINT(0, board.coinCount);

    TEST_ASSERT_TRUE(quotes::parseBoard("", 0, board));
    TEST_ASSERT_TRUE(quotes::parseBoard(nullptr, 0, board));
}

void testEmptyCoinArray() {
    const char *json = "{\"time\":\"t\",\"coins\":[]}";
    quotes::Board board;
    TEST_ASSERT_FALSE(quotes::parseBoard(json, strlen(json), board));
    TEST_ASSERT_EQUAL_UINT(0, board.coinCount);
    TEST_ASSERT_EQUAL_UINT(0, board.totalCoins);
    TEST_ASSERT_EQUAL_STRING("t", board.time);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(testReadmePayload);
    RUN_TEST(testLargePayloadWithoutTerminator);
    RUN_TEST(testFragmentFailsCleanly);
    RUN_TEST(testMissingAndNullFields);
    RUN_TEST(testOddCoinsShapes);
    RUN_TEST(testOverLongValuesAreTruncated);
    RUN_TEST(testMalformedAndEmptyInput);
    RUN_TEST(testEmptyCoinArray);
    return UNITY_END();
}
