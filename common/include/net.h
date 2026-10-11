#ifndef NET_H
#define NET_H

// Library Header Files
#include <string>
#include <vector>
#include <cstdint>

using namespace std;

class Net {
private:
    static int64_t nowMs();
    static bool waitFd(int fd, short events, int64_t deadline);
    static bool sendAll(int sockFd, const char* buf, size_t len, int64_t deadline);
    static bool recvAll(int sockFd, char* buf, size_t len, int64_t deadline);
    static void putNum(string& data, uint32_t num);
    static bool getNum(const string& data, size_t& pos, uint32_t& num);

public:
    static const size_t maxMsg = 8 * 1024 * 1024;

    static bool getNum(const string& str, uint64_t& num);
    static bool validAddr(const string& addr);
    static bool loadCfg(const char* path, vector<string>& addrs);
    static bool setTimeout(int sockFd);
    static int connectTo(const string& addr);
    static int listenAt(const string& addr);
    static string pack(const vector<string>& fields);
    static bool unpack(const string& data, vector<string>& fields);
    static bool sendMsg(int sockFd, const vector<string>& fields);
    static bool recvMsg(int sockFd, vector<string>& fields);
    static bool readFile(const string& path, string& data);
    static bool saveFile(const string& path, const string& data);
    static string getToken();
};

#endif