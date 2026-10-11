// Library Header Files
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

// Imported Files
#include "../include/sync.h"
#include "../../common/include/net.h"
#include "../../common/include/hash.h"

using namespace std;

string Entry::pack() const {

    vector<string> data;
    data.push_back(to_string(clock));
    data.push_back(to_string(trackerNo));
    data.push_back(reqId);
    data.push_back(Net::pack(args));
    
    return Net::pack(data);
}

bool Entry::unpack(const string& data) {

    vector<string> fields;
    uint64_t num;
    
    if (!Net::unpack(data, fields) || fields.size() != 4) 
        return false;

    if (!Net::getNum(fields[0], clock) || clock == 0 || clock == UINT64_MAX) 
        return false;

    if (!Net::getNum(fields[1], num) || num < 1 || num > 2) 
        return false;

    if (!Hash::valid(fields[2])) 
        return false;

    if (!Net::unpack(fields[3], args) || args.empty() || !State::isWrite(args[0])) 
        return false;

    trackerNo = static_cast<int>(num);
    reqId = fields[2];
    
    return true;
}

Sync::Sync() {
    trackerNo = 0;
    lockFd = -1;
    clock = 0;
    stopped = false;
}

Sync::~Sync() {
    if (lockFd != -1) 
        close(lockFd);
}

bool Sync::init(int num, const string& addr) {

    trackerNo = num;
    peerAddr = addr;
    logPath = "groupsTracker" + to_string(num) + ".log";
    const char* key = getenv("P2P_SYNC_KEY");
    syncKey = "a3-classroom";

    if (key != NULL) 
        syncKey = key;

    string lockPath = logPath + ".lock";
    lockFd = open(lockPath.c_str(), O_RDWR | O_CREAT, 0600);

    if (lockFd == -1 || flock(lockFd, LOCK_EX | LOCK_NB) == -1)
        return false;

    struct stat info;

    if (stat(logPath.c_str(), &info) == -1)
        return errno == ENOENT;

    string data;
    vector<string> saved;

    if (!Net::readFile(logPath, data) || data.size() < 40)
        return false;

    if (Hash::digest(data.substr(40)) != data.substr(0, 40))
        return false;

    if (!Net::unpack(data.substr(40), saved))
        return false;

    return mergeLog(saved, false);
}

vector<string> Sync::getLog() {

    vector<string> data;

    for (auto it = entries.begin(); it != entries.end(); it++) {
        data.push_back(it->second.pack());
    }

    return data;
}

void Sync::rebuild() {

    state = State();
    replies.clear();
    clock = 0;

    // map order is (logical clock, tracker number), identical at both trackers.
    for (auto it = entries.begin(); it != entries.end(); it++) {

        const Entry& entry = it->second;

        if (entry.clock > clock) 
            clock = entry.clock;

        // The same request can arrive at both trackers after a connection failure.
        if (replies.count(entry.reqId) != 0)
            continue;

        replies[entry.reqId] = state.runCmd(entry.args);
    }
}

bool Sync::saveLog() {

    string data = Net::pack(getLog());
    
    // Reserve space for the outer SYNC request and its framing fields.
    if (data.size() > Net::maxMsg - 4096)
        return false;

    return Net::saveFile(logPath, Hash::digest(data) + data);
}

bool Sync::mergeLog(const vector<string>& data, bool save) {

    vector<pair<uint64_t, int>> added;

    try {
        for (size_t i = 0; i < data.size(); i++) {

            Entry entry;

            if (!entry.unpack(data[i])) 
                throw runtime_error("Invalid log entry");

            pair<uint64_t, int> key(entry.clock, entry.trackerNo);
            auto it = entries.find(key);

            if (it != entries.end()) {

                if (it->second.pack() != data[i]) 
                    throw runtime_error("Conflicting log identity");
                
                continue;
            }

            // Record the key first so rollback also works if insertion throws.
            added.push_back(key);
            entries.insert(make_pair(key, entry));
        }

        if (added.empty())
            return true;

        rebuild();

        if (save && !saveLog()) 
            throw runtime_error("Could not save tracker log");

        return true;
    } 
    catch (const exception& err) {

        for (size_t i = 0; i < added.size(); i++) {
            entries.erase(added[i]);
        }

        rebuild();
        fprintf(stderr, "Sync: %s\n", err.what());
        return false;
    }
}

void Sync::syncPeer() {

    // Avoid two outgoing syncs at once. Never hold stateLock over network I/O.
    unique_lock<mutex> syncGuard(syncLock, try_to_lock);

    if (!syncGuard.owns_lock())
        return;

    string data;
    {
        lock_guard<mutex> guard(stateLock);
        data = Net::pack(getLog());
    }

    int sockFd = Net::connectTo(peerAddr);

    if (sockFd == -1)
        return; // The other tracker may be offline.

    vector<string> reply;
    bool ok;

    try {
        ok = Net::sendMsg(sockFd, {"SYNC", syncKey, data});
        
        if (ok) 
            ok = Net::recvMsg(sockFd, reply);
    } 
    catch (...) {
        close(sockFd);
        throw;
    }

    close(sockFd);

    if (!ok)
        return;

    if (reply.size() != 2 || reply[0] != "OK") {
        fprintf(stderr, "Sync: peer rejected update\n");
        return;
    }

    vector<string> incoming;

    if (!Net::unpack(reply[1], incoming))
        return;

    lock_guard<mutex> guard(stateLock);
    mergeLog(incoming, true);
}

vector<string> Sync::recvSync(const string& key, const string& data) {

    if (key != syncKey)
        return {"ERR", "AUTH", "Wrong tracker sync key"};

    vector<string> incoming;

    if (!Net::unpack(data, incoming))
        return {"ERR", "INPUT", "Invalid sync data"};

    lock_guard<mutex> guard(stateLock);

    if (!mergeLog(incoming, true))
        return {"ERR", "SYNC", "Could not merge tracker logs"};

    return {"OK", Net::pack(getLog())};
}

vector<string> Sync::runCmd(const string& reqId, const vector<string>& args) {

    if (args.empty() || !Hash::valid(reqId))
        return {"ERR", "INPUT", "Invalid request"};

    lock_guard<mutex> guard(stateLock);

    if (replies.count(reqId) != 0)
        return replies[reqId];

    if (!State::isWrite(args[0]))
        return state.runCmd(args);

    // Check the command on a copy before adding it to the durable log.
    State trial = state;
    vector<string> reply = trial.runCmd(args);

    if (reply[0] != "OK")
        return reply;

    if (clock >= UINT64_MAX - 1)
        return {"ERR", "LIMIT", "Logical clock exhausted"};

    Entry entry;
    entry.clock = clock + 1;
    entry.trackerNo = trackerNo;
    entry.reqId = reqId;
    entry.args = args;

    if (!mergeLog({entry.pack()}, true))
        return {"ERR", "SAVE", "Could not save command"};

    return replies[reqId];
}

void Sync::run() {

    while (!stopped) {
        
        try {
            syncPeer();
        } 
        catch (const exception& err) {
            fprintf(stderr, "Sync: %s\n", err.what());
        }

        for (int i = 0; i < 10 && !stopped; i++) {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void Sync::stop() {
    stopped = true;
}