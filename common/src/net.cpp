// Library Header Files
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Imported Files
#include "../include/net.h"
#include "../include/hash.h"

using namespace std;

bool Net::getNum(const string& str, uint64_t& num) {

    num = 0;

    if (str.empty())
        return false;

    for (size_t i = 0; i < str.size(); i++) {

        if (str[i] < '0' || str[i] > '9')
            return false;

        int digit = str[i] - '0';

        if (num > (UINT64_MAX - digit) / 10)
            return false;

        num = num * 10 + digit;
    }

    return true;
}

bool Net::validAddr(const string& addr) {

    if (addr.find('\0') != string::npos) 
        return false;

    size_t pos = addr.find(':');

    if (pos == string::npos)
        return false;

    uint64_t port;

    if (!getNum(addr.substr(pos + 1), port) || port == 0 || port > 65535)
        return false;

    struct in_addr ip;
    return inet_pton(AF_INET, addr.substr(0, pos).c_str(), &ip) == 1;
}

bool Net::loadCfg(const char* path, vector<string>& addrs) {

    string data;

    if (!readFile(path, data))
        return false;

    istringstream input(data);
    string line;
    addrs.clear();

    while (getline(input, line)) {

        line = line.substr(0, line.find('#'));
        istringstream row(line);
        string ip;
        string port;
        string extra;

        if (!(row >> ip))
            continue;

        if (ip.find(':') == string::npos) {
            
            if (!(row >> port))
                return false;

            ip += ":" + port;
        }

        if (row >> extra)
            return false;

        if (!validAddr(ip))
            return false;

        size_t pos = ip.find(':');
        uint64_t value;
        getNum(ip.substr(pos + 1), value);
        addrs.push_back(ip.substr(0, pos + 1) + to_string(value));
    }

    if (addrs.size() != 2)
        return false;

    return addrs[0] != addrs[1];
}

bool Net::setTimeout(int sockFd) {

    struct timeval tv;
    tv.tv_sec = 4;
    tv.tv_usec = 0;

    if (setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
        return false;

    return setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

int Net::connectTo(const string& addr) {

    if (!validAddr(addr))
        return -1;

    int sockFd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockFd == -1)
        return -1;

    size_t pos = addr.find(':');
    uint64_t port;
    getNum(addr.substr(pos + 1), port);
    
    struct sockaddr_in info = {};
    info.sin_family = AF_INET;
    info.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, addr.substr(0, pos).c_str(), &info.sin_addr);

    int flags = fcntl(sockFd, F_GETFL, 0);

    if (flags == -1 || fcntl(sockFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        close(sockFd);
        return -1;
    }

    int res = connect(sockFd, reinterpret_cast<sockaddr*>(&info), sizeof(info));

    if (res == -1 && errno == EINPROGRESS) {

        struct pollfd item = {sockFd, POLLOUT, 0};

        do {
            res = poll(&item, 1, 1000);
        } while (res == -1 && errno == EINTR);

        int err = 0;
        socklen_t len = sizeof(err);

        if (res <= 0 || getsockopt(sockFd, SOL_SOCKET, SO_ERROR, &err, &len) == -1 || err != 0) {
            close(sockFd);
            return -1;
        }
    } 
    else if (res == -1) {
        close(sockFd);
        return -1;
    }

    if (fcntl(sockFd, F_SETFL, flags) == -1 || !setTimeout(sockFd)) {
        close(sockFd);
        return -1;
    }

    return sockFd;
}

int Net::listenAt(const string& addr) {

    if (!validAddr(addr))
        return -1;

    int sockFd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockFd == -1)
        return -1;

    int opt = 1;
    size_t pos = addr.find(':');
    uint64_t port;
    getNum(addr.substr(pos + 1), port);
    
    struct sockaddr_in info = {};
    info.sin_family = AF_INET;
    info.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, addr.substr(0, pos).c_str(), &info.sin_addr);

    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1 || bind(sockFd, reinterpret_cast<sockaddr*>(&info), sizeof(info)) == -1 || listen(sockFd, 32) == -1) {
        close(sockFd);
        return -1;
    }

    return sockFd;
}

int64_t Net::nowMs() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
}

bool Net::waitFd(int fd, short events, int64_t deadline) {

    while (true) {

        int64_t left = deadline - nowMs();

        if (left <= 0) 
            return false;

        pollfd item{fd, events, 0};
        int count = poll(&item, 1, static_cast<int>(left));

        if (count == -1 && errno == EINTR) 
            continue;

        return count > 0 && (item.revents & events);
    }
}

bool Net::sendAll(int sockFd, const char* buf, size_t len, int64_t deadline) {

    size_t sent = 0;

    while (sent < len) {

        if (!waitFd(sockFd, POLLOUT, deadline)) 
            return false;

        ssize_t count = send(sockFd, buf + sent, len - sent, MSG_NOSIGNAL | MSG_DONTWAIT);

        if (count == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) 
            continue;

        if (count <= 0) 
            return false;

        sent += static_cast<size_t>(count);
    }

    return true;
}

bool Net::recvAll(int sockFd, char* buf, size_t len, int64_t deadline) {

    size_t received = 0;

    while (received < len) {

        if (!waitFd(sockFd, POLLIN, deadline)) 
            return false;

        ssize_t count = recv(sockFd, buf + received, len - received, MSG_DONTWAIT);

        if (count == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) 
            continue;

        if (count <= 0) 
            return false;

        received += static_cast<size_t>(count);
    }

    return true;
}

void Net::putNum(string& data, uint32_t num) {
    uint32_t value = htonl(num);
    data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

bool Net::getNum(const string& data, size_t& pos, uint32_t& num) {

    if (pos > data.size() || data.size() - pos < 4)
        return false;

    uint32_t value;
    memcpy(&value, data.data() + pos, 4);
    pos += 4;
    num = ntohl(value);
    
    return true;
}

string Net::pack(const vector<string>& fields) {

    string data;

    if (fields.size() > 100000) 
        throw runtime_error("Too many fields");

    putNum(data, static_cast<uint32_t>(fields.size()));

    for (size_t i = 0; i < fields.size(); i++) {

        if (fields[i].size() > maxMsg || data.size() + fields[i].size() + 4 > maxMsg) {
            throw runtime_error("Message too large");
        }

        putNum(data, static_cast<uint32_t>(fields[i].size()));
        data += fields[i];
    }

    return data;
}

bool Net::unpack(const string& data, vector<string>& fields) {

    size_t pos = 0;
    uint32_t count;
    fields.clear();

    if (!getNum(data, pos, count) || count > 100000 || count > (data.size() - pos) / 4)
        return false;

    for (uint32_t i = 0; i < count; i++) {

        uint32_t len;

        if (!getNum(data, pos, len) || len > data.size() - pos)
            return false;

        fields.push_back(data.substr(pos, len));
        pos += len;
    }

    return pos == data.size();
}

bool Net::sendMsg(int sockFd, const vector<string>& fields) {

    int64_t deadline = nowMs() + 10000;
    string data = pack(fields);
    string header = "P2P2";
    
    putNum(header, static_cast<uint32_t>(data.size()));
    header += Hash::digest(data);

    if (!sendAll(sockFd, header.data(), header.size(), deadline))
        return false;

    return sendAll(sockFd, data.data(), data.size(), deadline);
}

bool Net::recvMsg(int sockFd, vector<string>& fields) {

    int64_t deadline = nowMs() + 10000;
    string header(48, '\0');

    if (!recvAll(sockFd, &header[0], header.size(), deadline))
        return false;

    if (header.substr(0, 4) != "P2P2")
        return false;

    size_t pos = 4;
    uint32_t len;

    if (!getNum(header, pos, len) || len < 4 || len > maxMsg)
        return false;

    string data(len, '\0');

    if (!recvAll(sockFd, &data[0], len, deadline))
        return false;

    if (Hash::digest(data) != header.substr(8))
        return false;

    return unpack(data, fields);
}

bool Net::readFile(const string& path, string& data) {

    int fd = open(path.c_str(), O_RDONLY);

    if (fd == -1)
        return false;

    data.clear();
    char buf[4096];

    try {
        while (true) {

            ssize_t count = read(fd, buf, sizeof(buf));

            if (count == -1 && errno == EINTR)
                continue;

            if (count == 0)
                break;

            if (count < 0 || data.size() + count > maxMsg + 40) {
                close(fd);
                return false;
            }

            data.append(buf, static_cast<size_t>(count));
        }
    } 
    catch (...) {
        close(fd);
        throw;
    }

    close(fd);
    return true;
}

bool Net::saveFile(const string& path, const string& data) {

    string temp = path + ".tmp";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd == -1)
        return false;

    size_t written = 0;

    while (written < data.size()) {

        ssize_t count = write(fd, data.data() + written, data.size() - written);

        if (count == -1 && errno == EINTR)
            continue;

        if (count <= 0) {
            close(fd);
            unlink(temp.c_str());
            return false;
        }

        written += static_cast<size_t>(count);
    }

    bool ok = fsync(fd) == 0;

    if (close(fd) == -1) 
        ok = false;

    if (!ok || rename(temp.c_str(), path.c_str()) == -1) {
        unlink(temp.c_str());
        return false;
    }

    return true;
}

string Net::getToken() {

    int fd = open("/dev/urandom", O_RDONLY);

    if (fd == -1) 
        throw runtime_error("Cannot open random source");

    unsigned char bytes[20];
    size_t count = 0;

    while (count < sizeof(bytes)) {

        ssize_t len = read(fd, bytes + count, sizeof(bytes) - count);

        if (len == -1 && errno == EINTR)
            continue;

        if (len <= 0) {
            close(fd);
            throw runtime_error("Cannot read random source");
        }

        count += static_cast<size_t>(len);
    }

    close(fd);

    string token;
    char part[3];

    for (size_t i = 0; i < sizeof(bytes); i++) {
        snprintf(part, sizeof(part), "%02x", bytes[i]);
        token += part;
    }

    return token;
}