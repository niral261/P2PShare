#ifndef TRACKER_H
#define TRACKER_H

// Library Header Files
#include <queue>
#include <thread>
#include <condition_variable>

// Imported Files
#include "sync.h"

using namespace std;

class Tracker {
private:
    int listenFd;
    atomic<bool> stopped;
    Sync sync;
    queue<int> jobs;
    mutex jobLock;
    condition_variable jobReady;
    vector<thread> workers;
    thread syncThread;

    void serve();
    void stop();
    vector<string> handle(const vector<string>& msg);

public:
    Tracker();
    ~Tracker();
    bool run(const char* cfgPath, int trackerNo);
};

#endif