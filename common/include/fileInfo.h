#ifndef FILE_INFO_H
#define FILE_INFO_H

// Library Header Files
#include <string>
#include <vector>
#include <cstdint>

using namespace std;

// One owner for an open descriptor, including exception paths.
class Fd {
private:
    int fd;

public:
    explicit Fd(int value = -1);
    ~Fd();
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const;
};

class FileInfo {
public:
    static constexpr size_t pieceSize = 512 * 1024;
    static constexpr uint64_t maxSize = 1024ULL * 1024 * 1024;
    
    string name;
    string hash;
    uint64_t size = 0;
    vector<string> pieces;

    static bool validName(const string& name);
    static string fullPath(const string& path);
    static string destPath(const string& path, const string& name);
    static string readAt(int fd, size_t len, uint64_t offset);
    static void writeAt(int fd, const string& data, uint64_t offset);
    static FileInfo scan(int fd, const string& name);
    static FileInfo decode(const vector<string>& fields, size_t pos = 0);
    
    vector<string> encode() const;
    size_t partSize(size_t idx) const;
};

#endif