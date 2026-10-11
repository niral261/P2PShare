#include <exception>

// Imported Files
#include "../include/state.h"
#include "../../common/include/net.h"
#include "../../common/include/hash.h"

using namespace std;

bool State::validId(const string& id) {

    if (id.empty() || id.size() > 64) 
        return false;
    
    for (size_t i = 0; i < id.size(); i++) {
        char ch = id[i];
        bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool digit = ch >= '0' && ch <= '9';
        
        if (!letter && !digit && ch != '_' && ch != '-') 
            return false;
    }

    return true;
}

string State::getUser(const string& token) {

    if (token.empty()) 
        return "";
    
    for (auto it = users.begin(); it != users.end(); it++) {
        if (it->second.token == token) 
            return it->first;
    }

    return "";
}

bool State::isWrite(const string& cmd) {

    return cmd == "create_user" || cmd == "login" || cmd == "logout" || cmd == "create_group" || cmd == "join_group" || cmd == "leave_group" || cmd == "accept_request" || cmd == "upload_file" || cmd == "start_share" || cmd == "stop_share";
}

vector<string> State::createUser(const vector<string>& args) {

    string uid = args[1];

    if (!validId(uid)) 
        return {"ERR", "INPUT", "Invalid user ID"};

    if (!Hash::valid(args[2])) 
        return {"ERR", "INPUT", "Invalid password digest"};

    if (users.count(uid) != 0) 
        return {"ERR", "EXISTS", "User already exists"};

    User user;
    user.passHash = args[2];
    users[uid] = user;

    return {"OK", "User created"};
}

vector<string> State::login(const vector<string>& args) {

    string uid = args[1];
    auto it = users.find(uid);
    
    if (it == users.end() || it->second.passHash != args[2]) {
        return {"ERR", "AUTH", "Invalid user ID or password"};
    }
    
    if (!Net::validAddr(args[3]) || !Hash::valid(args[4])) {
        return {"ERR", "INPUT", "Invalid login details"};
    }

    removeShares(uid);
    
    // A fresh login replaces the user's previous session on both trackers
    it->second.addr = args[3];
    it->second.token = args[4];

    return {"OK", args[4]};
}

vector<string> State::logout(const string& uid) {

    removeShares(uid);
    users[uid].token.clear();
    users[uid].addr.clear();

    return {"OK", "Logged out"};
}

vector<string> State::createGroup(const string& gid, const string& uid) {

    if (!validId(gid)) 
        return {"ERR", "INPUT", "Invalid group ID"};

    if (groups.count(gid) != 0) 
        return {"ERR", "EXISTS", "Group already exists"};

    Group grp;
    grp.owner = uid;
    grp.members.insert(uid);
    groups[gid] = grp;

    return {"OK", "Group created"};
}

vector<string> State::joinGroup(const string& gid, const string& uid) {

    auto it = groups.find(gid);

    if (it == groups.end()) 
        return {"ERR", "NOT_FOUND", "Group does not exist"};

    Group& grp = it->second;

    if (grp.members.count(uid) != 0) 
        return {"ERR", "EXISTS", "Already a member"};

    if (grp.pending.count(uid) != 0) 
        return {"ERR", "EXISTS", "Request already pending"};

    grp.pending.insert(uid);

    return {"OK", "Join request sent"};
}

vector<string> State::leaveGroup(const string& gid, const string& uid) {

    auto it = groups.find(gid);

    if (it == groups.end()) 
        return {"ERR", "NOT_FOUND", "Group does not exist"};

    Group& grp = it->second;

    if (grp.members.count(uid) == 0) 
        return {"ERR", "ACCESS", "You are not a member"};

    for (auto f = grp.files.begin(); f != grp.files.end(); f++) {
        f->second.seeders.erase(uid);
    }

    grp.members.erase(uid);

    if (grp.members.empty()) {
        groups.erase(it);
        return {"OK", "Left group; empty group deleted"};
    }

    // A set gives the same next owner on both trackers during log replay
    if (grp.owner == uid) {
        grp.owner = *grp.members.begin();
        return {"OK", "Left group; new owner: " + grp.owner};
    }

    return {"OK", "Left group"};
}

vector<string> State::listGroups() {

    vector<string> reply = {"OK"};

    for (auto it = groups.begin(); it != groups.end(); it++) {
        reply.push_back(it->first + " (owner: " + it->second.owner + ")");
    }

    return reply;
}

vector<string> State::listRequests(const string& gid, const string& uid) {

    auto it = groups.find(gid);

    if (it == groups.end()) 
        return {"ERR", "NOT_FOUND", "Group does not exist"};

    Group& grp = it->second;

    if (grp.owner != uid) 
        return {"ERR", "ACCESS", "Only the owner can list requests"};

    vector<string> reply = {"OK"};

    for (auto req = grp.pending.begin(); req != grp.pending.end(); req++) {
        reply.push_back(*req);
    }

    return reply;
}

vector<string> State::acceptRequest(const string& gid, const string& uid, const string& reqUid) {

    auto it = groups.find(gid);

    if (it == groups.end()) 
        return {"ERR", "NOT_FOUND", "Group does not exist"};

    Group& grp = it->second;

    if (grp.owner != uid) 
        return {"ERR", "ACCESS", "Only the owner can accept requests"};

    if (grp.pending.count(reqUid) == 0) 
        return {"ERR", "NOT_FOUND", "No pending request"};

    grp.pending.erase(reqUid);
    grp.members.insert(reqUid);

    return {"OK", "Request accepted"};
}

vector<string> State::runCmd(const vector<string>& args) {

    if (args.empty()) 
        return {"ERR", "INPUT", "Empty command"};

    string cmd = args[0];
    size_t count = 0;

    if (cmd == "create_user") {
        count = 3;
    } 
    else if (cmd == "login") {
        count = 5;
    } 
    else if (cmd == "logout" || cmd == "list_groups") {
        count = 2;
    } 
    else if (cmd == "create_group" || cmd == "join_group" || cmd == "leave_group" || cmd == "list_requests") {
        count = 3;
    } 
    else if (cmd == "list_files") {
        count = 3;
    } 
    else if (cmd == "peers" || cmd == "stop_share") {
        count = 4;
    } 
    else if (cmd == "start_share") {
        count = 5;
    } 
    else if (cmd == "authorize") {
        count = 6;
    } 
    else if (cmd == "upload_file") {
        count = 7;
    } 
    else if (cmd == "accept_request") {
        count = 4;
    } 
    else {
        return {"ERR", "INPUT", "Unknown command"};
    }

    if (args.size() != count) 
        return {"ERR", "INPUT", "Wrong argument count"};

    if (cmd == "create_user") 
        return createUser(args);

    if (cmd == "login") 
        return login(args);

    string uid = getUser(args[1]);

    if (uid.empty()) 
        return {"ERR", "AUTH", "Login required or session expired"};

    if (cmd == "logout") 
        return logout(uid);

    if (cmd == "list_groups") 
        return listGroups();

    if (cmd == "create_group") 
        return createGroup(args[2], uid);

    if (cmd == "join_group") 
        return joinGroup(args[2], uid);

    if (cmd == "leave_group") 
        return leaveGroup(args[2], uid);

    if (cmd == "list_requests") 
        return listRequests(args[2], uid);
    
    if (cmd == "accept_request")
        return acceptRequest(args[2], uid, args[3]);

    return fileCmd(args, uid);
}

void State::removeShares(const string& uid) {

    for (auto g = groups.begin(); g != groups.end(); g++) {

        for (auto f = g->second.files.begin(); f != g->second.files.end(); f++) {
            f->second.seeders.erase(uid);
        }
    }
}

vector<string> State::fileCmd(const vector<string>& args, const string& uid) {

    auto g = groups.find(args[2]);

    if (g == groups.end()) 
        return {"ERR", "NOT_FOUND", "Group does not exist"};

    Group& grp = g->second;

    if (grp.members.count(uid) == 0) 
        return {"ERR", "ACCESS", "Group membership required"};

    string cmd = args[0];

    if (cmd == "list_files") {

        vector<string> reply = {"OK"};

        for (auto it = grp.files.begin(); it != grp.files.end(); it++) {

            if (!it->second.seeders.empty())
                reply.push_back(it->first + " (" + to_string(it->second.info.size) + " bytes)");
        }

        return reply;
    }

    if (cmd == "upload_file") {

        FileInfo info;

        try {
            info = FileInfo::decode(args, 3);
        } 
        catch (const exception& err) {
            return {"ERR", "INPUT", err.what()};
        }

        auto it = grp.files.find(info.name);

        if (it != grp.files.end() && it->second.info.encode() != info.encode())
            return {"ERR", "EXISTS", "A different file already has this name in the group"};

        SharedFile& file = grp.files[info.name];
        file.info = info;
        file.seeders[uid] = args[1];

        return {"OK", "File shared"};
    }

    auto it = grp.files.find(args[3]);

    if (it == grp.files.end()) 
        return {"ERR", "NOT_FOUND", "File not found"};

    SharedFile& file = it->second;

    if (cmd == "stop_share") {

        file.seeders.erase(uid);
        return {"OK", "Sharing stopped"};
    }

    if (cmd == "start_share") {

        if (file.info.hash != args[4]) 
            return {"ERR", "VERSION", "File version changed"};

        file.seeders[uid] = args[1];
        return {"OK", "Peer registered"};
    }

    if (cmd == "peers") {

        vector<string> reply = {"OK"};
        vector<string> meta = file.info.encode();
        reply.insert(reply.end(), meta.begin(), meta.end());

        for (auto seed = file.seeders.begin(); seed != file.seeders.end(); seed++) {

            auto user = users.find(seed->first);

            if (user != users.end() && !user->second.token.empty() && user->second.token == seed->second)
                reply.push_back(user->second.addr);
        }

        return reply;
    }

    if (cmd == "authorize") {

        string seeder = getUser(args[5]);

        if (file.info.hash != args[4] || seeder.empty() || grp.members.count(seeder) == 0 || file.seeders.count(seeder) == 0 || file.seeders[seeder] != args[5])
            return {"ERR", "ACCESS", "Peer is no longer sharing this file"};

        return {"OK", "Allowed"};
    }

    return {"ERR", "INPUT", "Unknown file command"};
}