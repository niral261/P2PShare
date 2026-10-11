// Library Header Files
#include <cstring>
#include <cstdio>

// Imported Files
#include "../include/hash.h"

using namespace std;

Hash::Hash() {
    state[0] = 0x67452301;
    state[1] = 0xefcdab89;
    state[2] = 0x98badcfe;
    state[3] = 0x10325476;
    state[4] = 0xc3d2e1f0;
    byteCount = 0;
    used = 0;
    memset(buf, 0, sizeof(buf));
}

uint32_t Hash::rotate(uint32_t val, int count) {
    return (val << count) | (val >> (32 - count));
}

void Hash::processBlock(const unsigned char* data) {

    uint32_t words[80];

    for (int i = 0; i < 16; i++) {
        words[i] = (uint32_t(data[4 * i]) << 24) | (uint32_t(data[4 * i + 1]) << 16) | (uint32_t(data[4 * i + 2]) << 8) | uint32_t(data[4 * i + 3]);
    }

    for (int i = 16; i < 80; i++) {
        words[i] = rotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; i++) {

        uint32_t val;
        uint32_t key;

        if (i < 20) {
            val = (b & c) | (~b & d);
            key = 0x5a827999;
        } 
        else if (i < 40) {
            val = b ^ c ^ d;
            key = 0x6ed9eba1;
        } 
        else if (i < 60) {
            val = (b & c) | (b & d) | (c & d);
            key = 0x8f1bbcdc;
        } 
        else {
            val = b ^ c ^ d;
            key = 0xca62c1d6;
        }

        uint32_t temp = rotate(a, 5) + val + e + key + words[i];
        e = d;
        d = c;
        c = rotate(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void Hash::update(const void* data, size_t len) {

    const unsigned char* ptr = static_cast<const unsigned char*>(data);
    byteCount += len;

    while (len > 0) {

        size_t count = 64 - used;
        
        if (count > len) 
            count = len;

        memcpy(buf + used, ptr, count);
        used += count;
        ptr += count;
        len -= count;

        if (used == 64) {
            processBlock(buf);
            used = 0;
        }
    }
}

string Hash::finish() const {

    Hash copy = *this;
    uint64_t bits = byteCount * 8;
    unsigned char pad = 0x80;
    unsigned char zero = 0;
    
    copy.update(&pad, 1);

    while (copy.used != 56) {
        copy.update(&zero, 1);
    }

    unsigned char tail[8];

    for (int i = 0; i < 8; i++) {
        tail[7 - i] = static_cast<unsigned char>(bits >> (8 * i));
    }

    copy.update(tail, 8);

    string result;
    char part[9];

    for (int i = 0; i < 5; i++) {
        snprintf(part, sizeof(part), "%08x", copy.state[i]);
        result += part;
    }

    return result;
}

string Hash::digest(const string& data) {
    Hash hash;
    hash.update(data.data(), data.size());
    return hash.finish();
}

bool Hash::valid(const string& hash) {

    if (hash.size() != 40)
        return false;

    for (size_t i = 0; i < hash.size(); i++) {
        char ch = hash[i];
        
        if (!(ch >= '0' && ch <= '9') && !(ch >= 'a' && ch <= 'f'))
            return false;
    }

    return true;
}