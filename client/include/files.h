#ifndef FILES_H
#define FILES_H

#include <map>
#include <memory>
#include <mutex>
#include "../../common/include/fileInfo.h"

using namespace std;

class LocalFile {
private:
    mutex fileLock;
    string available;
    bool enabled = true;
    Fd fd; // Construct last: ownership begins after metadata allocation succeeds.

public:
    const FileInfo info;
    const string token;

    LocalFile(int fd, const FileInfo& info, const string& token, bool complete);
    int getFd() const;
    string bitmap();
    string piece(size_t idx);
    void mark(size_t idx);
    void disable();
};

class Files {
private:
    mutex filesLock;
    map<pair<string, string>, shared_ptr<LocalFile>> files;

public:
    void add(const string& gid, const shared_ptr<LocalFile>& file);
    shared_ptr<LocalFile> get(const string& gid, const string& name);
    void remove(const string& gid, const string& name);
    void clear(const string& gid = "");
};

#endif