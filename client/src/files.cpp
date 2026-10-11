// Library Header Files
#include <stdexcept>

// Imported Files
#include "../include/files.h"
#include "../../common/include/hash.h"

// Constructor for LocalFile
LocalFile::LocalFile(int value, const FileInfo& data, const string& session, bool complete)
    : available(data.pieces.size(), complete ? '1' : '0'), info(data), token(session), fd(value) {}

// Get file descriptor
int LocalFile::getFd() const { 
    return fd.get(); 
}

// Get availability bitmap
string LocalFile::bitmap() {

    lock_guard<mutex> guard(fileLock);

    if (!enabled) 
        throw runtime_error("Sharing stopped");

    return available;
}

// Get a specific piece of the file
string LocalFile::piece(size_t idx) {

    lock_guard<mutex> guard(fileLock);

    if (!enabled || idx >= available.size() || available[idx] != '1')
        throw runtime_error("Piece unavailable");

    string data = FileInfo::readAt(
        fd.get(), 
        info.partSize(idx), 
        idx * FileInfo::pieceSize
    );

    if (Hash::digest(data) != info.pieces[idx]) {
        available[idx] = '0';
        throw runtime_error("Source file changed");
    }

    return data;
}

// Mark a piece as available
void LocalFile::mark(size_t idx) {

    lock_guard<mutex> guard(fileLock);

    if (idx < available.size()) 
        available[idx] = '1';
}

// Disable sharing for this file
void LocalFile::disable() {

    lock_guard<mutex> guard(fileLock);
    enabled = false;
}

// Add a file to shared store
void Files::add(const string& gid, const shared_ptr<LocalFile>& file) {

    lock_guard<mutex> guard(filesLock);

    auto key = make_pair(gid, file->info.name);

    if (files.count(key)) 
        throw runtime_error("Already sharing or downloading this file; stop_share first");

    files[key] = file;
}

// Get a file from shared store
shared_ptr<LocalFile> Files::get(const string& gid, const string& name) {

    lock_guard<mutex> guard(filesLock);

    auto it = files.find({gid, name});

    if (it == files.end()) 
        return nullptr;

    return it->second;
}

// Remove a file from shared store
void Files::remove(const string& gid, const string& name) {

    lock_guard<mutex> guard(filesLock);

    auto it = files.find({gid, name});

    if (it == files.end()) 
        return;

    it->second->disable();
    files.erase(it);
}

// Clear files for a specific group
void Files::clear(const string& gid) {

    lock_guard<mutex> guard(filesLock);

    for (auto it = files.begin(); it != files.end();) {

        if (gid.empty() || it->first.first == gid) {
            it->second->disable();
            it = files.erase(it);
        } 
        else {
            ++it;
        }
    }
}