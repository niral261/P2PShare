// Library Header Files
#include <cstdio>
#include <exception>

// Imported Files
#include "../include/tracker.h"
#include "../../common/include/net.h"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(stderr, "Usage: ./tracker tracker_info.txt <1|2>\n");
        return 1;
    }

    uint64_t trackerNo;

    if (!Net::getNum(argv[2], trackerNo) || trackerNo < 1 || trackerNo > 2) {
        fprintf(stderr, "Tracker number must be 1 or 2\n");
        return 1;
    }

    try {
        Tracker tracker;

        if (!tracker.run(argv[1], static_cast<int>(trackerNo)))
            return 1;
    } 
    catch (const exception& err) {
        fprintf(stderr, "Tracker: %s\n", err.what());
        return 1;
    }

    return 0;
}