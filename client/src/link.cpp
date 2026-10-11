// Library Header Files
#include <stdexcept>

// Imported Files
#include "../include/link.h"
#include "../../common/include/net.h"
#include "../../common/include/fileInfo.h"

// Initialize link with configuration
bool Link::init(const char* path) { 
    return Net::loadCfg(path, trackers); 
}

// Send request to trackers
vector<string> Link::request(const vector<string>& args) {

    vector<string> msg = {
        "CMD", 
        Net::getToken(), 
        Net::pack(args)
    };

    int first = active.load();

    for (int i = 0; i < 2; i++) {

        int idx = (first + i) % 2;
        Fd fd(Net::connectTo(trackers[idx]));

        if (fd.get() == -1) 
            continue;

        vector<string> reply;

        if (Net::sendMsg(fd.get(), msg) && 
            Net::recvMsg(fd.get(), reply) &&
            !reply.empty() && 
            (reply[0] == "OK" || reply[0] == "ERR")) {
            
            active = idx;
            return reply;
        }
    }

    return {
        "ERR", 
        "NETWORK", 
        "Both trackers unavailable; a write may already have committed."
    };
}

// Send checked request to trackers
vector<string> Link::checked(const vector<string>& args) {

    vector<string> reply = request(args);

    if (reply.empty() || reply[0] != "OK")
        throw runtime_error(reply.size() >= 3 ? reply[2] : "Invalid tracker reply");

    return reply;
}