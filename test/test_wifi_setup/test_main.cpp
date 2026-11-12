// Tests for the Wi-Fi provisioning helpers.
//
// Run with: pio test -e native

#include <unity.h>

#include <string>

#include "wifi_setup.h"

namespace {

// Deterministic stand-in for esp_random(), so generated passwords are
// reproducible in tests.
//
// splitmix64 rather than a plain LCG: an LCG's low 5 bits repeat with period 32,
// and 8 consecutive 12-character windows line up with that period, which would
// make the generator look broken when it is the test double that is weak. The
// device uses esp_random(), a hardware RNG, so its low bits are uniform.
struct SequenceRng {
    uint64_t state;
    uint32_t operator()() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<uint32_t>((z ^ (z >> 31)) >> 32);
    }
};

struct ConstantRng {
    uint32_t value;
    uint32_t operator()() { return value; }
};

std::string generate(ConstantRng rng) {  // temporary: forwarded, not copied
    char password[wifi_setup::kPasswordBufferSize];
    TEST_ASSERT_TRUE(wifi_setup::makePassword(password, sizeof(password), rng));
    return std::string(password);
}

}  // namespace

// The generated password must always be something WPA2 (and therefore
// WiFiManager's access point) will actually accept.
void testGeneratedPasswordIsAcceptable() {
    SequenceRng rng{12345ull};
    for (int i = 0; i < 200; i++) {
        char password[wifi_setup::kPasswordBufferSize];
        TEST_ASSERT_TRUE(wifi_setup::makePassword(password, sizeof(password), rng));
        TEST_ASSERT_EQUAL_UINT(wifi_setup::kPasswordLength, strlen(password));
        TEST_ASSERT_TRUE(wifi_setup::isAcceptablePassword(password));
    }
}

// Only unambiguous characters: no 0/O or 1/I/l to misread on the display.
void testGeneratedPasswordAvoidsConfusableCharacters() {
    SequenceRng rng{999ull};
    const char *forbidden = "01OIl";
    for (int i = 0; i < 100; i++) {
        char password[wifi_setup::kPasswordBufferSize];
        TEST_ASSERT_TRUE(wifi_setup::makePassword(password, sizeof(password), rng));
        for (const char *c = password; *c; c++) {
            TEST_ASSERT_EQUAL(nullptr, strchr(forbidden, *c));
            TEST_ASSERT_NOT_EQUAL(nullptr, strchr(wifi_setup::passwordAlphabet(), *c));
        }
    }
}

// The whole alphabet is reachable and the mapping is the documented mask, so
// there is no modulo bias and no unreachable character.
void testPasswordUsesWholeAlphabet() {
    bool seen[wifi_setup::kAlphabetSize] = {false};
    SequenceRng rng{7ull};
    const char *alphabet = wifi_setup::passwordAlphabet();
    for (int i = 0; i < 500; i++) {
        char password[wifi_setup::kPasswordBufferSize];
        TEST_ASSERT_TRUE(wifi_setup::makePassword(password, sizeof(password), rng));
        for (const char *c = password; *c; c++)
            seen[strchr(alphabet, *c) - alphabet] = true;
    }
    for (size_t i = 0; i < wifi_setup::kAlphabetSize; i++)
        TEST_ASSERT_TRUE_MESSAGE(seen[i], "alphabet character never produced");
}

// Deterministic mapping: the low 5 bits of the RNG select the character.
void testPasswordCharacterMappingIsExact() {
    TEST_ASSERT_EQUAL_STRING("222222222222", generate(ConstantRng{0}).c_str());
    TEST_ASSERT_EQUAL_STRING("ZZZZZZZZZZZZ", generate(ConstantRng{31}).c_str());
    TEST_ASSERT_EQUAL_STRING("222222222222",
                             generate(ConstantRng{32}).c_str());  // wraps
    TEST_ASSERT_EQUAL_STRING("ZZZZZZZZZZZZ",
                             generate(ConstantRng{0xFFFFFFFFu}).c_str());
}

// Two devices must not get the same password from the same seedless source;
// here simply check that a sequence of draws is not a single repeated value.
void testSuccessivePasswordsDiffer() {
    SequenceRng rng{4242ull};
    char first[wifi_setup::kPasswordBufferSize];
    TEST_ASSERT_TRUE(wifi_setup::makePassword(first, sizeof(first), rng));
    for (int i = 0; i < 50; i++) {
        char next[wifi_setup::kPasswordBufferSize];
        TEST_ASSERT_TRUE(wifi_setup::makePassword(next, sizeof(next), rng));
        TEST_ASSERT_TRUE(strcmp(first, next) != 0);
    }
}

// A buffer of exactly the required size works; anything smaller is refused
// rather than truncated into a weak password.
void testBufferSizeBoundary() {
    char exact[wifi_setup::kPasswordBufferSize];
    TEST_ASSERT_TRUE(
        wifi_setup::makePassword(exact, sizeof(exact), ConstantRng{1}));

    char tooSmall[wifi_setup::kPasswordBufferSize - 1];
    TEST_ASSERT_FALSE(
        wifi_setup::makePassword(tooSmall, sizeof(tooSmall), ConstantRng{1}));

    char empty[1];
    TEST_ASSERT_FALSE(wifi_setup::makePassword(empty, sizeof(empty), ConstantRng{1}));
    TEST_ASSERT_FALSE(wifi_setup::makePassword(nullptr, 16, ConstantRng{1}));
}

// The validator matches WPA2-PSK rules, which is why the portal password
// length is what it is.
void testAcceptablePasswordBounds() {
    TEST_ASSERT_FALSE(wifi_setup::isAcceptablePassword(nullptr));
    TEST_ASSERT_FALSE(wifi_setup::isAcceptablePassword(""));
    TEST_ASSERT_FALSE(wifi_setup::isAcceptablePassword("1234567"));      // 7
    TEST_ASSERT_TRUE(wifi_setup::isAcceptablePassword("12345678"));      // 8
    TEST_ASSERT_TRUE(wifi_setup::isAcceptablePassword(
        "012345678901234567890123456789012345678901234567890123456789012"));  // 63
    TEST_ASSERT_FALSE(wifi_setup::isAcceptablePassword(
        "0123456789012345678901234567890123456789012345678901234567890123"));  // 64
}

// Secrets are described, never printed.
void testDescribeSecretNeverRevealsTheSecret() {
    char out[32];
    TEST_ASSERT_TRUE(wifi_setup::describeSecret("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("unset", out);
    TEST_ASSERT_TRUE(wifi_setup::describeSecret(nullptr, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("unset", out);

    TEST_ASSERT_TRUE(wifi_setup::describeSecret("sup3rSecret", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("11 chars", out);
    TEST_ASSERT_EQUAL(nullptr, strstr(out, "sup3r"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(testGeneratedPasswordIsAcceptable);
    RUN_TEST(testGeneratedPasswordAvoidsConfusableCharacters);
    RUN_TEST(testPasswordUsesWholeAlphabet);
    RUN_TEST(testPasswordCharacterMappingIsExact);
    RUN_TEST(testSuccessivePasswordsDiffer);
    RUN_TEST(testBufferSizeBoundary);
    RUN_TEST(testAcceptablePasswordBounds);
    RUN_TEST(testDescribeSecretNeverRevealsTheSecret);
    return UNITY_END();
}
