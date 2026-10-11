// Library Header Files
#include <cstdio>
#include <exception>

// Imported Files
#include "../include/client.h"

using namespace std;

// Main entry point
int main(int argc, char* argv[]) {

    if (argc != 3) {
        fprintf(
            stderr, 
            "Usage: ./client <IP>:<PORT> tracker_info.txt\n"
        );
        return 1;
    }

    try {
        Client client;

        if (!client.run(argv[1], argv[2]))
            return 1;
    } 
    catch (const exception& err) {
        
        fprintf(
            stderr, 
            "Client: %s\n", 
            err.what()
        );
        return 1;
    }

    return 0;
}