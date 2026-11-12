// Wi-Fi provisioning helpers.
//
// The board is configured once through a captive portal, so no credentials live
// in the source tree or the firmware image. These helpers cover the parts worth
// testing without hardware: the portal access-point password (which protects the
// provisioning window) and the description of a secret for logs.
//
// Kept free of Arduino / ESP-IDF dependencies so the same code the firmware
// compiles can be exercised on a development machine.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace wifi_setup {

// WPA2-PSK accepts 8 to 63 printable ASCII characters. A shorter access-point
// password is silently discarded by the Wi-Fi stack, which would leave the
// provisioning portal wide open, so this bound is enforced rather than assumed.
constexpr size_t kMinPasswordLength = 8;
constexpr size_t kMaxPasswordLength = 63;

// Confusable-free alphabet: 0/O and 1/I/l are left out so the password can be
// read off a small screen and typed on a phone without mistakes. Exactly 32
// characters, so drawing a character with `& 0x1F` is uniform (no modulo bias).
inline const char *passwordAlphabet() { return "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"; }

constexpr size_t kAlphabetSize = 32;
constexpr size_t kPasswordLength = 12;  // 32^12 = 2^60 combinations
constexpr size_t kPasswordBufferSize = kPasswordLength + 1;

// Bytes needed to hold a generated password, including the terminator.
inline bool isAcceptablePassword(const char *password) {
    if (password == nullptr)
        return false;
    const size_t length = strlen(password);
    if (length < kMinPasswordLength || length > kMaxPasswordLength)
        return false;
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = static_cast<unsigned char>(password[i]);
        if (c < 32 || c > 126)
            return false;  // WPA2 passphrase must be printable ASCII
    }
    return true;
}

// Fills `out` with a random password from the alphabet above.
//
// `rng` returns a uniformly distributed 32-bit value (esp_random() on the
// device). It is forwarded rather than copied: a stateful generator passed as
// an lvalue must advance between calls, otherwise every password would be the
// same. Only the low 5 bits are used, and because the alphabet has exactly 32
// entries each character is uniformly likely.
template <typename Rng>
inline bool makePassword(char *out, size_t outSize, Rng &&rng) {
    if (out == nullptr || outSize < kPasswordBufferSize)
        return false;

    static_assert(kAlphabetSize == 32, "mask below assumes a 32 character alphabet");
    const char *alphabet = passwordAlphabet();
    for (size_t i = 0; i < kPasswordLength; i++)
        out[i] = alphabet[rng() & 0x1Fu];
    out[kPasswordLength] = '\0';
    return true;
}

// Describes a secret for logging without revealing it: "unset" or "12 chars".
// Log statements use this so a stray edit cannot print a password.
inline bool describeSecret(const char *secret, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    if (secret == nullptr || secret[0] == '\0')
        return snprintf(out, outSize, "unset") > 0;
    return snprintf(out, outSize, "%u chars",
                    static_cast<unsigned>(strlen(secret))) > 0;
}

}  // namespace wifi_setup
