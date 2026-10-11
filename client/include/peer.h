#ifndef PEER_H
#define PEER_H

#include <atomic>
#include <condition_variable>
#include <queue>
#include <thread>
#include "files.h"
#include "link.h"

class Peer {
private:
    Link& link;
    Files& files;
    int listenFd = -1;
    atomic<bool> stopped{true};
    thread acceptThread;
    vector<thread> workers;
    queue<int> jobs;
    mutex jobLock;
    condition_variable ready;

    void acceptLoop();
    void serve();
    vector<string> handle(const vector<string>& args);

public:
    Peer(Link& link, Files& files);
    ~Peer();
    bool start(const string& addr);
    void stop();
};

#endif