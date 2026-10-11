#ifndef CLIENT_H
#define CLIENT_H

// Library Header Files
#include <string>
#include <vector>
#include "download.h"
#include "peer.h"

using namespace std;

class Client {
private:
    Link link;
    Files files;
    Peer peer;
    Downloads downloads;
    
    string addr;
    string token;

    void upload(const string& gid, const string& path);
    bool parseLine(const string& line, vector<string>& args);
    vector<string> sendCmd(const vector<string>& args);
    bool runCmd(const vector<string>& args);
    void showReply(const vector<string>& reply);
    void showHelp();
    void logout();

public:
    Client();
    ~Client();
    bool run(const char* clientAddr, const char* cfgPath);
};

#endif