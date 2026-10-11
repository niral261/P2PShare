#ifndef LINK_H
#define LINK_H

#include <atomic>
#include <string>
#include <vector>

using namespace std;

class Link {
private:
    vector<string> trackers;
    atomic<int> active{0};

public:
    bool init(const char* path);
    vector<string> request(const vector<string>& args);
    vector<string> checked(const vector<string>& args);
};

#endif