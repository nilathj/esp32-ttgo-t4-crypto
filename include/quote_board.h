// Parsing of the crypto price JSON payload into a small, fixed-size snapshot.
//
// This header only needs ArduinoJson. The ESP-IDF call it makes is guarded, so
// the exact parsing code that runs on the TTGO T4 can also be compiled and
// tested on a development machine with an ordinary C++ compiler.

#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace quotes {

// ArduinoJson allocates on the heap while parsing. On the TTGO T4 the PSRAM is
// the right home for that temporary work: a large payload then never competes
// with the Wi-Fi/MQTT stacks for internal RAM. Falls back to the normal heap
// when PSRAM is absent or exhausted.
class BoardAllocator : public ArduinoJson::Allocator {
  public:
    void *allocate(size_t size) override {
#if defined(ESP_PLATFORM)
        void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr != nullptr)
            return ptr;
#endif
        return malloc(size);
    }

    void deallocate(void *ptr) override {
        // ESP-IDF routes free() through the same heap-caps registry that owns
        // PSRAM blocks, so one call handles both kinds of allocation.
        free(ptr);
    }

    void *reallocate(void *ptr, size_t newSize) override {
        if (ptr == nullptr)
            return allocate(newSize);
#if defined(ESP_PLATFORM)
        // Grow in place inside PSRAM when possible, so a long coin list does
        // not migrate onto the internal heap mid-parse. On failure the
        // original block is untouched, and the plain heap is tried last.
        void *grown = heap_caps_realloc(
            ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (grown != nullptr)
            return grown;
#endif
        return realloc(ptr, newSize);
    }
};

inline ArduinoJson::Allocator *boardAllocator() {
    static BoardAllocator allocator;
    return &allocator;
}

// Never keep more coins than the display could ever show. This bounds the
// snapshot (and the work done per message), no matter how long the server's
// coin list grows.
constexpr size_t kMaxCoins = 16;

struct Coin {
    char name[12];
    char price[16];
    char change[10];  // price_change_24h, e.g. "-2.2%"
    char volume[10];
};

struct Board {
    char time[24];
    size_t coinCount;   // coins copied into `coins`
    size_t totalCoins;  // coins present in the payload (may exceed coinCount)
    Coin coins[kMaxCoins];
};

inline void copyField(char *dst, size_t dstSize, const char *src) {
    const size_t maxLen = dstSize - 1;
    size_t len = src ? strlen(src) : 0;
    if (len > maxLen)
        len = maxLen;
    if (len > 0)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

// Deserialises `json` (which does not have to be null terminated) into `out`.
//
// A filter is used so that only the four displayed fields are materialised and
// unrelated payload content is skipped. Together with ArduinoJson 7's elastic
// JsonDocument this keeps the heap cost proportional to the number of coins
// rather than to the size of the whole payload; a payload that still does not
// fit fails with DeserializationError::NoMemory instead of corrupting memory.
inline DeserializationError parseBoard(const char *json, size_t len,
                                       Board &out) {
    out.time[0] = '\0';
    out.coinCount = 0;
    out.totalCoins = 0;
    memset(out.coins, 0, sizeof(out.coins));

    if (json == nullptr || len == 0)
        return DeserializationError(DeserializationError::EmptyInput);

    // Select the fields we display. An array filter applies to every element,
    // so this keeps {"name","price",...} for each coin and drops the rest.
    JsonDocument filter(boardAllocator());
    filter["time"] = true;
    filter["coins"][0]["name"] = true;
    filter["coins"][0]["price"] = true;
    filter["coins"][0]["price_change_24h"] = true;
    filter["coins"][0]["vol"] = true;

    JsonDocument doc(boardAllocator());
    DeserializationError error =
        deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
    if (error)
        return error;

    copyField(out.time, sizeof(out.time), doc["time"] | "");

    JsonArrayConst coins = doc["coins"].as<JsonArrayConst>();
    out.totalCoins = coins.size();

    size_t count = 0;
    for (size_t i = 0; i < out.totalCoins && count < kMaxCoins; i++) {
        JsonObjectConst coin = coins[i].as<JsonObjectConst>();
        if (coin.isNull())
            continue; // ignore entries that are not objects, don't draw blanks

        Coin &entry = out.coins[count];
        copyField(entry.name, sizeof(entry.name), coin["name"] | "");
        copyField(entry.price, sizeof(entry.price), coin["price"] | "");
        copyField(entry.change, sizeof(entry.change),
                  coin["price_change_24h"] | "");
        copyField(entry.volume, sizeof(entry.volume), coin["vol"] | "");
        count++;
    }
    out.coinCount = count;

    return DeserializationError::Ok;
}

}  // namespace quotes
