// Library Header Files
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

// Imported Files
#include "../include/tracker.h"
#include "../../common/include/net.h"

using namespace std;

Tracker::Tracker() {
    listenFd = -1;
    stopped = false;
}

Tracker::~Tracker() {
    stop();
}

vector<string> Tracker::handle(const vector<string>& msg) {

    if (msg.size() != 3)
        return {"ERR", "INPUT", "Invalid message"};

    if (msg[0] == "SYNC")
        return sync.recvSync(msg[1], msg[2]);

    if (msg[0] != "CMD")
        return {"ERR", "INPUT", "Unknown message type"};

    vector<string> args;

    if (!Net::unpack(msg[2], args) || args.empty())
        return {"ERR", "INPUT", "Invalid command"};

    sync.syncPeer();
    vector<string> reply = sync.runCmd(msg[1], args);

    if (reply[0] == "OK" && State::isWrite(args[0])) {
        sync.syncPeer();
        // A concurrent conflicting write might have lost during reconciliation.
        reply = sync.runCmd(msg[1], args);
    }

    return reply;
}

void Tracker::serve() {

    while (true) {
        
        int sockFd;
        {
            unique_lock<mutex> guard(jobLock);
            
            while (jobs.empty() && !stopped) {
                jobReady.wait(guard);
            }

            if (stopped)
                return;

            sockFd = jobs.front();
            jobs.pop();
        }

        try {
            vector<string> msg;

            if (Net::setTimeout(sockFd) && Net::recvMsg(sockFd, msg)) {
                Net::sendMsg(sockFd, handle(msg));
            }
        } 
        catch (const exception& err) {
            fprintf(stderr, "Request: %s\n", err.what());

            try {
                Net::sendMsg(sockFd, {"ERR", "SERVER", "Could not process request"});
            } 
            catch (...) {
                // The socket is still closed below if an error reply also fails.
            }
        }

        close(sockFd);
    }
}

void Tracker::stop() {

    {
        lock_guard<mutex> guard(jobLock);
        stopped = true;
    }

    sync.stop();
    jobReady.notify_all();

    if (syncThread.joinable()) {
        syncThread.join();
    }

    for (size_t i = 0; i < workers.size(); i++) {
        if (workers[i].joinable()) {
            workers[i].join();
        }
    }

    while (!jobs.empty()) {
        close(jobs.front());
        jobs.pop();
    }

    if (listenFd != -1) {
        close(listenFd);
        listenFd = -1;
    }
}

bool Tracker::run(const char* cfgPath, int trackerNo) {

    vector<string> addrs;

    if (trackerNo < 1 || trackerNo > 2 || !Net::loadCfg(cfgPath, addrs)) {
        fprintf(stderr, "Need two tracker addresses and tracker number 1 or 2\n");
        return false;
    }

    if (!sync.init(trackerNo, addrs[2 - trackerNo])) {
        fprintf(stderr, "Could not load or lock tracker log\n");
        return false;
    }

    listenFd = Net::listenAt(addrs[trackerNo - 1]);

    if (listenFd == -1) {
        perror("listen");
        return false;
    }

    // Four workers are enough for this assignment's small client count.
    // A bounded queue avoids creating one thread for every connection.
    workers.reserve(4);

    for (int i = 0; i < 4; i++) {
        workers.emplace_back(&Tracker::serve, this);
    }

    syncThread = thread(&Sync::run, &sync);
    printf("Tracker %d listening on %s. Type quit to stop.\n", trackerNo, addrs[trackerNo - 1].c_str());
    fflush(stdout);

    string line;

    while (!stopped) {

        struct pollfd items[2] = {{listenFd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        int res = poll(items, 2, 200);

        if (res == -1 && errno == EINTR)
            continue;

        if (res == -1) {
            perror("poll");
            return false;
        }

        if (items[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
            
            char buf[128];
            ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));

            if (len <= 0) {
                if (len == -1 && errno == EINTR)
                    continue;
                break;
            }

            for (ssize_t i = 0; i < len; i++) {

                if (buf[i] == '\n') {
                    
                    if (line == "quit") 
                        stopped = true;
                    else if (!line.empty()) 
                        printf("Tracker command: quit\n");
                    
                    line.clear();
                } 
                else if (buf[i] != '\r' && line.size() < 128) {
                    line += buf[i];
                }
            }
        }

        if (!stopped && (items[0].revents & POLLIN)) {

            int sockFd = accept(listenFd, NULL, NULL);

            if (sockFd == -1)
                continue;

            try {
                lock_guard<mutex> guard(jobLock);

                if (jobs.size() >= 32) {
                    close(sockFd);
                } 
                else {
                    jobs.push(sockFd);
                    jobReady.notify_one();
                }
            } 
            catch (...) {
                close(sockFd);
                throw;
            }
        }
    }

    stop();
    return true;
}