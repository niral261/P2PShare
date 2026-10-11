#ifndef STATE_H
#define STATE_H

// Library Header Files
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../common/include/fileInfo.h"

using namespace std;

class User {
public:
    string passHash;
    string token;
    string addr;
};

class SharedFile {
public:
    FileInfo info;
    map<string, string> seeders; // user ID -> announcing session token
};

class Group {
public:
    string owner;
    set<string> members;
    set<string> pending;
    map<string, SharedFile> files;
};

class State {
private:
    map<string, User> users;
    map<string, Group> groups;

    void removeShares(const string& uid);
    vector<string> fileCmd(const vector<string>& args, const string& uid);
    bool validId(const string& id);
    string getUser(const string& token);
    vector<string> createUser(const vector<string>& args);
    vector<string> login(const vector<string>& args);
    vector<string> logout(const string& uid);
    vector<string> createGroup(const string& gid, const string& uid);
    vector<string> joinGroup(const string& gid, const string& uid);
    vector<string> leaveGroup(const string& gid, const string& uid);
    vector<string> listGroups();
    vector<string> listRequests(const string& gid, const string& uid);
    vector<string> acceptRequest(const string& gid, const string& uid, const string& reqUid);

public:
    static bool isWrite(const string& cmd);
    vector<string> runCmd(const vector<string>& args);
};

#endif