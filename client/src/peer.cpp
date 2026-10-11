// Library Header Files
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <stdexcept>

// Imported Files
#include "../include/peer.h"
#include "../../common/include/net.h"

// Constructor
Peer::Peer(Link& conn, Files& store) 
    : link(conn), files(store) {}

// Destructor
Peer::~Peer() { 
    stop(); 
}

// Start peer server
bool Peer::start(const string& addr) {

    listenFd = Net::listenAt(addr);

    if (listenFd == -1) 
        return false;

    stopped = false;

    try {
        for (int i = 0; i < 8; i++) 
            workers.emplace_back(&Peer::serve, this);

        acceptThread = thread(&Peer::acceptLoop, this);
    } 
    catch (...) { 
        stop(); 
        throw; 
    }

    return true;
}

// Stop peer server
void Peer::stop() {

    {
        lock_guard<mutex> guard(jobLock);
        stopped = true;
    }

    ready.notify_all();

    if (acceptThread.joinable()) 
        acceptThread.join();

    for (thread& worker : workers) {
        if (worker.joinable()) 
            worker.join();
    }

    workers.clear();

    while (!jobs.empty()) { 
        close(jobs.front()); 
        jobs.pop(); 
    }

    if (listenFd != -1) 
        close(listenFd);

    listenFd = -1;
}

// Accept incoming connections
void Peer::acceptLoop() {

    while (!stopped) {

        pollfd pfd{listenFd, POLLIN, 0};

        if (poll(&pfd, 1, 200) <= 0 || !(pfd.revents & POLLIN)) 
            continue;

        int fd = accept(listenFd, nullptr, nullptr);

        if (fd == -1) 
            continue;

        lock_guard<mutex> guard(jobLock);

        if (jobs.size() >= 32 || stopped) {
            close(fd);
        }
        else { 
            jobs.push(fd); 
            ready.notify_one(); 
        }
    }
}

// Serve connections from the queue
void Peer::serve() {

    while (true) {

        int sock;

        {
            unique_lock<mutex> guard(jobLock);
            ready.wait(guard, [this] { 
                return stopped || !jobs.empty(); 
            });

            if (stopped) 
                return;

            sock = jobs.front(); 
            jobs.pop();
        }

        Fd fd(sock);
        Net::setTimeout(sock);

        try {
            vector<string> args;

            if (Net::recvMsg(sock, args)) 
                Net::sendMsg(sock, handle(args));
        } 
        catch (const exception& err) {
            
            try { 
                Net::sendMsg(sock, {"ERR", err.what()}); 
            } 
            catch (...) {
            }
        }
    }
}

// Handle peer request
vector<string> Peer::handle(const vector<string>& args) {

    if (args.empty() || 
        (args[0] != "BITMAP" && args[0] != "PIECE") ||
        args.size() != (args[0] == "BITMAP" ? 5U : 6U)) {
        
        throw runtime_error("Invalid peer request");
    }

    auto file = files.get(args[2], args[3]);

    if (!file || file->info.hash != args[4]) 
        throw runtime_error("File unavailable");

    link.checked({
        "authorize", 
        args[1], 
        args[2], 
        args[3], 
        args[4], 
        file->token
    });

    if (args[0] == "BITMAP") 
        return {"OK", file->bitmap()};

    uint64_t idx;

    if (!Net::getNum(args[5], idx) || idx >= file->info.pieces.size())
        throw runtime_error("Invalid piece number");

    return {"OK", file->piece(idx)};
}