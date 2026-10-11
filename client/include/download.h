#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <atomic>
#include <thread>
#include "files.h"
#include "link.h"

class Download {
private:
    Link& link;
    Files& files;
    shared_ptr<LocalFile> file;
    string token, addr, gid, name, dest, temp;
    vector<string> peers;
    atomic<size_t> next{0}, done{0};
    atomic<bool> cancelled{false}, failed{false}, finished{false};
    mutex statusLock, shareLock;
    string status = "D", error;
    bool sharing = true;
    thread job;

    vector<string> findPeers();
    bool fetch(size_t idx);
    void work();
    void run();

public:
    Download(Link& link, Files& files, const string& token, const string& addr, const string& gid, const string& name, const string& dest);
    ~Download();
    void start();
    void cancel();
    void join();
    void stopSharing();
    bool matches(const string& gid, const string& name = "") const;
    bool isFinished() const;
    void show();
};

class Downloads {
private:
    Link& link;
    Files& files;
    vector<unique_ptr<Download>> jobs;

public:
    Downloads(Link& link, Files& files);
    ~Downloads();
    void start(const string& token, const string& addr, const string& gid, const string& name, const string& dest);
    void cancel(const string& gid = "");
    void stopSharing(const string& gid, const string& name);
    void show();
};

#endif