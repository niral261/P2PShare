// Library Header Files
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>

// Imported Files
#include "../include/fileInfo.h"
#include "../include/hash.h"
#include "../include/net.h"

Fd::Fd(int value) : fd(value) {}

Fd::~Fd() {
    if (fd != -1) 
        close(fd);
}

int Fd::get() const {
    return fd;
}

bool FileInfo::validName(const string& name) {

    if (name.empty() || name.size() > 255 || name == "." || name == "..")
        return false;

    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '/' || static_cast<unsigned char>(name[i]) < 32)
            return false;
    }

    return true;
}

string FileInfo::fullPath(const string& path) {

    if (path.empty() || path.find('\0') != string::npos)
        throw runtime_error("Invalid path");

    char* result = realpath(path.c_str(), NULL);

    if (result == NULL)
        throw runtime_error("Cannot resolve path: " + string(strerror(errno)));

    string full;

    try {
        full = result;
    } 
    catch (...) {
        free(result);
        throw;
    }

    free(result);
    return full;
}

string FileInfo::destPath(const string& path, const string& name) {

    if (path.empty() || path.find('\0') != string::npos || !validName(name))
        throw runtime_error("Invalid destination");

    struct stat info;

    if (stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
        return fullPath(path) + "/" + name;

    size_t pos = path.find_last_of('/');
    string parent = ".";
    string base = path;

    if (pos != string::npos) {
        parent = pos == 0 ? "/" : path.substr(0, pos);
        base = path.substr(pos + 1);
    }

    if (!validName(base)) 
        throw runtime_error("Invalid destination filename");

    return fullPath(parent) + "/" + base;
}

string FileInfo::readAt(int fd, size_t len, uint64_t offset) {

    string data(len, '\0');
    size_t done = 0;

    while (done < len) {

        ssize_t count = pread(fd, &data[done], len - done, static_cast<off_t>(offset + done));

        if (count == -1 && errno == EINTR) 
            continue;

        if (count <= 0) 
            throw runtime_error("File read failed or source was truncated");

        done += static_cast<size_t>(count);
    }

    return data;
}

void FileInfo::writeAt(int fd, const string& data, uint64_t offset) {

    size_t done = 0;

    while (done < data.size()) {

        ssize_t count = pwrite(fd, data.data() + done, data.size() - done, static_cast<off_t>(offset + done));

        if (count == -1 && errno == EINTR) 
            continue;

        if (count <= 0) 
            throw runtime_error("File write failed: " + string(strerror(errno)));

        done += static_cast<size_t>(count);
    }
}

FileInfo FileInfo::scan(int fd, const string& name) {

    struct stat before, after;

    if (fstat(fd, &before) == -1 || !S_ISREG(before.st_mode) || before.st_size < 0 || static_cast<uint64_t>(before.st_size) > maxSize || !validName(name))
        throw runtime_error("Expected a regular file of at most 1 GiB with a valid name");

    FileInfo info;
    info.name = name;
    info.size = static_cast<uint64_t>(before.st_size);
    Hash whole;

    for (uint64_t offset = 0; offset < info.size; offset += pieceSize) {
        string data = readAt(fd, static_cast<size_t>(min<uint64_t>(pieceSize, info.size - offset)), offset);
        info.pieces.push_back(Hash::digest(data));
        whole.update(data.data(), data.size());
    }

    info.hash = whole.finish();

    if (fstat(fd, &after) == -1 || before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        throw runtime_error("File changed during hashing; retry after writing finishes");

    return info;
}

vector<string> FileInfo::encode() const {
    return {name, to_string(size), hash, Net::pack(pieces)};
}

FileInfo FileInfo::decode(const vector<string>& fields, size_t pos) {

    if (pos > fields.size() || fields.size() - pos < 4)
        throw runtime_error("Missing file metadata");

    FileInfo info;
    info.name = fields[pos];
    info.hash = fields[pos + 2];

    if (!validName(info.name) || !Net::getNum(fields[pos + 1], info.size) || info.size > maxSize || !Hash::valid(info.hash) || !Net::unpack(fields[pos + 3], info.pieces) || info.pieces.size() != (info.size + pieceSize - 1) / pieceSize)
        throw runtime_error("Invalid file metadata");

    for (size_t i = 0; i < info.pieces.size(); i++) {
        if (!Hash::valid(info.pieces[i])) 
            throw runtime_error("Invalid piece hash");
    }

    if (info.size == 0 && info.hash != Hash::digest(""))
        throw runtime_error("Invalid empty-file hash");

    return info;
}

size_t FileInfo::partSize(size_t idx) const {

    if (idx >= pieces.size()) 
        throw runtime_error("Piece index out of range");

    return static_cast<size_t>(min<uint64_t>(pieceSize, size - idx * pieceSize));
}