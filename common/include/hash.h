#ifndef HASH_H
#define HASH_H

// Library Header Files
#include <string>
#include <cstdint>
#include <cstddef>

using namespace std;

// Retained from the previous project for password digests and message checksums.
class Hash {
private:
    uint32_t state[5];
    unsigned char buf[64];
    uint64_t byteCount;
    size_t used;

    static uint32_t rotate(uint32_t val, int count);
    void processBlock(const unsigned char* data);

public:
    Hash();
    void update(const void* data, size_t len);
    string finish() const;
    static string digest(const string& data);
    static bool valid(const string& hash);
};

#endif