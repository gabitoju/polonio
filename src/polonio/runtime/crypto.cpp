#include "polonio/runtime/crypto.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#if defined(__APPLE__)
#include <stdlib.h>
#elif defined(__linux__)
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#endif

namespace polonio {

namespace {

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

struct SHA256State {
    std::array<uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint64_t, 2> length = {0, 0};
    std::array<uint8_t, 64> buffer{};
    std::size_t buffer_len = 0;
};

const uint32_t kSHA256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_transform(SHA256State& state, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state.h[0];
    uint32_t b = state.h[1];
    uint32_t c = state.h[2];
    uint32_t d = state.h[3];
    uint32_t e = state.h[4];
    uint32_t f = state.h[5];
    uint32_t g = state.h[6];
    uint32_t h = state.h[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + kSHA256K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state.h[0] += a;
    state.h[1] += b;
    state.h[2] += c;
    state.h[3] += d;
    state.h[4] += e;
    state.h[5] += f;
    state.h[6] += g;
    state.h[7] += h;
}

void sha256_update(SHA256State& state, const uint8_t* data, std::size_t len) {
    state.length[0] += len * 8;
    if (state.length[0] < len * 8) {
        state.length[1]++;
    }
    for (std::size_t i = 0; i < len; ++i) {
        state.buffer[state.buffer_len++] = data[i];
        if (state.buffer_len == 64) {
            sha256_transform(state, state.buffer.data());
            state.buffer_len = 0;
        }
    }
}

std::string sha256_final(SHA256State& state) {
    uint8_t pad = 0x80;
    sha256_update(state, &pad, 1);
    uint8_t zero = 0x00;
    while (state.buffer_len != 56) {
        sha256_update(state, &zero, 1);
    }
    uint8_t length_bytes[8];
    for (int i = 0; i < 8; ++i) {
        length_bytes[i] = static_cast<uint8_t>((state.length[1 - (i / 4)] >> ((i % 4) * 8)) & 0xFF);
    }
    sha256_update(state, length_bytes, 8);
    std::string digest(32, '\0');
    for (int i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = static_cast<char>((state.h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<char>((state.h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<char>((state.h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<char>(state.h[i] & 0xFF);
    }
    return digest;
}

std::string sha256(const std::string& data) {
    SHA256State state;
    sha256_update(state, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return sha256_final(state);
}

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string base64_encode_standard(const std::string& input) {
    std::string out;
    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) |
                         (static_cast<uint8_t>(input[i + 1]) << 8) |
                         static_cast<uint8_t>(input[i + 2]);
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[chunk & 0x3F]);
        i += 3;
    }
    std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        uint32_t chunk = static_cast<uint8_t>(input[i]) << 16;
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) |
                         (static_cast<uint8_t>(input[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

bool base64_decode_standard(const std::string& input, std::string& output) {
    int bits_collected = 0;
    uint32_t accumulator = 0;
    for (char ch : input) {
        if (ch == '=') {
            break;
        }
        int value = base64_index(ch);
        if (value < 0) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                continue;
            }
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            output.push_back(static_cast<char>((accumulator >> bits_collected) & 0xFF));
        }
    }
    return true;
}

} // namespace

std::string base64url_encode(const std::string& input) {
    std::string standard = base64_encode_standard(input);
    std::string out;
    out.reserve(standard.size());
    for (char ch : standard) {
        if (ch == '+') {
            out.push_back('-');
        } else if (ch == '/') {
            out.push_back('_');
        } else if (ch == '=') {
            continue;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

bool base64url_decode(const std::string& input, std::string& output) {
    std::string padded = input;
    for (char& ch : padded) {
        if (ch == '-') {
            ch = '+';
        } else if (ch == '_') {
            ch = '/';
        }
    }
    while (padded.size() % 4 != 0) {
        padded.push_back('=');
    }
    return base64_decode_standard(padded, output);
}

std::string hmac_sha256(const std::string& key, const std::string& data) {
    std::string normalized_key = key;
    if (normalized_key.size() > 64) {
        normalized_key = sha256(normalized_key);
    }
    normalized_key.resize(64, '\0');
    std::string o_key_pad(64, '\0');
    std::string i_key_pad(64, '\0');
    for (int i = 0; i < 64; ++i) {
        o_key_pad[i] = static_cast<char>(normalized_key[i] ^ 0x5c);
        i_key_pad[i] = static_cast<char>(normalized_key[i] ^ 0x36);
    }
    std::string inner = sha256(i_key_pad + data);
    return sha256(o_key_pad + inner);
}

bool secure_random_bytes(std::string& output, std::size_t size) {
    output.resize(size);
#if defined(__APPLE__)
    arc4random_buf(output.data(), size);
    return true;
#elif defined(__linux__)
    ssize_t ret = getrandom(output.data(), size, 0);
    if (ret == static_cast<ssize_t>(size)) {
        return true;
    }
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return false;
    }
    std::size_t total = 0;
    while (total < size) {
        ssize_t chunk = ::read(fd, output.data() + total, size - total);
        if (chunk <= 0) {
            ::close(fd);
            return false;
        }
        total += static_cast<std::size_t>(chunk);
    }
    ::close(fd);
    return true;
#else
#error "secure random not supported on this platform"
#endif
}

} // namespace polonio
