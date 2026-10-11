// Library Header Files
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <chrono>
#include <stdexcept>

// Imported Files
#include "../include/download.h"
#include "../../common/include/net.h"
#include "../../common/include/hash.h"

// Constructor
Download::Download(Link& conn, Files& store, const string& session, const string& endpoint, const string& group, const string& filename, const string& path) 
    : link(conn), files(store), token(session), addr(endpoint), gid(group), name(filename), dest(path) {}

// Destructor
Download::~Download() {
    cancel();
    join();
}

// Start download thread
void Download::start() {
    job = thread(&Download::run, this);
}

// Cancel download
void Download::cancel() {
    cancelled = true;
}

// Join download thread
void Download::join() {
    if (job.joinable())
        job.join();
}

// Check if finished
bool Download::isFinished() const {
    return finished.load();
}

// Check if matches group and filename
bool Download::matches(const string& group, const string& filename) const {
    return (group.empty() || group == gid) && (filename.empty() || filename == name);
}

// Stop sharing file
void Download::stopSharing() {
    lock_guard<mutex> guard(shareLock);
    sharing = false;
    files.remove(gid, name);
}

// Find peers from tracker
vector<string> Download::findPeers() {

    vector<string> reply = link.checked({"peers", token, gid, name});
    FileInfo info = FileInfo::decode(reply, 1);

    if (file && info.encode() != file->info.encode())
        throw runtime_error("Tracker metadata changed");

    vector<string> result;

    for (size_t i = 5; i < reply.size(); i++) {
        if (reply[i] != addr && Net::validAddr(reply[i]))
            result.push_back(reply[i]);
    }

    return result;
}

// Fetch a piece of the file
bool Download::fetch(size_t idx) {

    vector<string> sources = peers;

    for (int round = 0; round < 4 && !cancelled && !failed; round++) {

        for (size_t i = 0; i < sources.size() && !cancelled && !failed; i++) {

            const string& source = sources[(idx + i) % sources.size()];
            vector<string> base = {"BITMAP", token, gid, name, file->info.hash};
            vector<string> reply;
            Fd bitmapFd(Net::connectTo(source));

            if (bitmapFd.get() == -1 || !Net::sendMsg(bitmapFd.get(), base) || !Net::recvMsg(bitmapFd.get(), reply) || reply.size() != 2 || reply[0] != "OK" || reply[1].size() != file->info.pieces.size() || reply[1][idx] != '1') {
                continue;
            }

            if (cancelled)
                return false;

            base[0] = "PIECE";
            base.push_back(to_string(idx));
            Fd pieceFd(Net::connectTo(source));

            if (pieceFd.get() == -1 || !Net::sendMsg(pieceFd.get(), base) || !Net::recvMsg(pieceFd.get(), reply) || reply.size() != 2 || reply[0] != "OK") {
                continue;
            }

            if (reply[1].size() != file->info.partSize(idx) || Hash::digest(reply[1]) != file->info.pieces[idx]) {
                continue;
            }

            FileInfo::writeAt(file->getFd(), reply[1], idx * FileInfo::pieceSize);
            file->mark(idx);
            done++;

            return true;
        }

        if (round != 3 && !cancelled && !failed) {
            this_thread::sleep_for(chrono::milliseconds(250));
            sources = findPeers();
        }
    }

    return false;
}

// Worker thread function
void Download::work() {

    try {
        while (!cancelled && !failed) {

            size_t idx = next.fetch_add(1);

            if (idx >= file->info.pieces.size())
                return;

            if (!fetch(idx))
                throw runtime_error("Piece " + to_string(idx) + " unavailable after retries");
        }
    }
    catch (const exception& err) {

        lock_guard<mutex> guard(statusLock);

        if (!failed.exchange(true))
            error = err.what();
    }
}

// Main download logic
void Download::run() {

    bool added = false;
    bool created = false;

    try {
        vector<string> reply = link.checked({"peers", token, gid, name});
        FileInfo info = FileInfo::decode(reply, 1);
        dest = FileInfo::destPath(dest, name);

        struct stat stats;
        
        if (lstat(dest.c_str(), &stats) == 0)
            throw runtime_error("Destination already exists");

        temp = dest + ".part." + Net::getToken();
        int fd = open(temp.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);

        if (fd == -1)
            throw runtime_error("Cannot create download temporary file");

        created = true;

        try {
            file = make_shared<LocalFile>(fd, info, token, false);
        }
        catch (...) {
            close(fd);
            throw;
        }

        if (ftruncate(fd, info.size) == -1)
            throw runtime_error("Cannot size destination file");

        peers = findPeers();

        {
            lock_guard<mutex> guard(shareLock);

            if (sharing && !cancelled) {
                files.add(gid, file);
                added = true;
                link.checked({"start_share", token, gid, name, info.hash});
            }
        }

        vector<thread> workers;

        try {
            for (int i = 0; i < 4; i++)
                workers.emplace_back(&Download::work, this);
        }
        catch (...) {
            cancelled = true;

            for (thread& worker : workers)
                worker.join();

            throw;
        }

        for (thread& worker : workers)
            worker.join();

        if (cancelled)
            throw runtime_error("Cancelled");

        if (failed)
            throw runtime_error(error);

        if (fsync(fd) == -1)
            throw runtime_error("Cannot flush download");

        FileInfo actual = FileInfo::scan(fd, name);

        if (actual.encode() != info.encode())
            throw runtime_error("Whole-file SHA1 mismatch");

        // link() publishes without replacing an existing destination
        if (::link(temp.c_str(), dest.c_str()) == -1)
            throw runtime_error("Cannot publish destination (possibly already exists)");

        unlink(temp.c_str());
        created = false;

        lock_guard<mutex> guard(statusLock);
        status = "C";
    }
    catch (const exception& err) {

        {
            lock_guard<mutex> guard(shareLock);

            if (added && sharing) {
                files.remove(gid, name);

                try {
                    link.request({"stop_share", token, gid, name});
                }
                catch (...) {
                }
            }
        }

        if (created)
            unlink(temp.c_str());

        lock_guard<mutex> guard(statusLock);
        status = "F";
        error = err.what();
    }

    file.reset();
    peers.clear();
    finished = true;
}

// Show download status
void Download::show() {

    lock_guard<mutex> guard(statusLock);

    printf("[%s] [%s] %s", status.c_str(), gid.c_str(), name.c_str());

    if (status == "D")
        printf(" - %zu pieces verified", done.load());

    if (status == "F")
        printf(" - %s", error.c_str());

    printf("\n");
}

// Downloads Constructor
Downloads::Downloads(Link& conn, Files& store) : link(conn), files(store) {}

// Downloads Destructor
Downloads::~Downloads() { 
    cancel(); 
}

// Start a new download job
void Downloads::start(const string& token, const string& addr, const string& gid, const string& name, const string& dest) {

    size_t active = 0;

    for (auto& job : jobs) {
        if (job->isFinished()) {
            job->join();
        }
        else {
            active++;

            if (job->matches(gid, name))
                throw runtime_error("This download is already running");
        }
    }

    if (active >= 3)
        throw runtime_error("Three downloads are already running");

    if (files.get(gid, name))
        throw runtime_error("Already sharing this file; stop_share first");

    auto job = make_unique<Download>(link, files, token, addr, gid, name, dest);
    
    job->start();
    jobs.push_back(move(job));
}

// Cancel all downloads in a group
void Downloads::cancel(const string& gid) {

    for (auto& job : jobs) {
        if (job->matches(gid))
            job->cancel();
    }

    for (auto& job : jobs) {
        if (job->matches(gid))
            job->join();
    }
}

// Stop sharing a specific file
void Downloads::stopSharing(const string& gid, const string& name) {

    for (auto& job : jobs) {
        if (job->matches(gid, name))
            job->stopSharing();
    }
}

// Show all downloads
void Downloads::show() {

    if (jobs.empty())
        printf("No downloads\n");

    for (auto& job : jobs)
        job->show();
}