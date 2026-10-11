#ifndef SYNC_H
#define SYNC_H

// Library Header Files
#include <atomic>
#include <mutex>
#include <cstdint>

// Imported Files
#include "state.h"

using namespace std;

class Entry {
public:
    uint64_t clock = 0;
    int trackerNo = 0;
    string reqId;
    vector<string> args;

    string pack() const;
    bool unpack(const string& data);
};

class Sync {
private:
    int trackerNo;
    int lockFd;
    uint64_t clock;
    string peerAddr;
    string logPath;
    string syncKey;
    State state;
    map<pair<uint64_t, int>, Entry> entries;
    map<string, vector<string>> replies;
    mutex stateLock;
    mutex syncLock;
    atomic<bool> stopped;

    // These helpers are called only while stateLock is held (or during init).
    vector<string> getLog();
    void rebuild();
    bool saveLog();
    bool mergeLog(const vector<string>& data, bool save);

public:
    Sync();
    ~Sync();
    
    bool init(int num, const string& addr);
    void syncPeer();
    void run();
    void stop();
    
    vector<string> recvSync(const string& key, const string& data);
    vector<string> runCmd(const string& reqId, const vector<string>& args);
};

#endif