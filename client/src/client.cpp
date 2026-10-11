// Library Header Files
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>

// Imported Files
#include "../include/client.h"
#include "../../common/include/net.h"
#include "../../common/include/hash.h"

using namespace std;

// Constructor
Client::Client()
    : peer(link, files), downloads(link, files) {}

// Destructor
Client::~Client() {
    downloads.cancel();
    files.clear();

    try {
        if (!token.empty())
            logout();
    }
    catch (const exception& e) {
        fprintf(stderr, "Logout: %s\n", e.what());
    }

    peer.stop();
}

// Convert command line into arguments
bool Client::parseLine(const string& line, vector<string>& args) {
    args.clear();

    string word;
    char quote = '\0';
    bool started = false;

    for (char ch : line) {

        // Inside quotes
        if (quote != '\0') {
            if (ch == quote)
                quote = '\0';
            else
                word += ch;

            started = true;
            continue;
        }

        // Start quote
        if (ch == '"' || ch == '\'') {
            quote = ch;
            started = true;
            continue;
        }

        // Space or tab means end of argument
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            if (started) {
                args.push_back(word);
                word.clear();
                started = false;
            }
            continue;
        }

        word += ch;
        started = true;
    }

    // Quote was not closed
    if (quote != '\0')
        return false;

    if (started)
        args.push_back(word);

    // Convert commands such as:
    // create user -> create_user
    // create group -> create_group
    // etc.
    if (args.size() >= 2) {
        string command = args[0] + "_" + args[1];

        if (command == "create_user" ||
            command == "create_group" ||
            command == "join_group" ||
            command == "leave_group" ||
            command == "list_groups" ||
            command == "list_requests" ||
            command == "accept_request" ||
            command == "upload_file" ||
            command == "list_files" ||
            command == "download_file" ||
            command == "show_downloads" ||
            command == "stop_share") {

            args[0] = command;
            args.erase(args.begin() + 1);
        }
    }

    return true;
}

// Send command to tracker
vector<string> Client::sendCmd(const vector<string>& args) {
    return link.request(args);
}

// Upload a file
void Client::upload(const string& gid, const string& path) {

    string fullPath = FileInfo::fullPath(path);

    size_t pos = fullPath.find_last_of('/');
    string fileName = fullPath.substr(pos + 1);

    int fd = open(fullPath.c_str(), O_RDONLY | O_NONBLOCK);

    if (fd == -1)
        throw runtime_error("Cannot open source file");

    shared_ptr<LocalFile> file;

    try {
        FileInfo info = FileInfo::scan(fd, fileName);
        file = make_shared<LocalFile>(fd, info, token, true);
    }
    catch (...) {
        close(fd);
        throw;
    }

    files.add(gid, file);

    try {
        vector<string> command = {"upload_file", token, gid};
        vector<string> fileInfo = file->info.encode();

        command.insert(command.end(), fileInfo.begin(), fileInfo.end());

        showReply(link.checked(command));
    }
    catch (...) {
        files.remove(gid, fileName);
        throw;
    }
}

// Display tracker response
void Client::showReply(const vector<string>& reply) {

    if (reply.empty())
        return;

    if (reply[0] == "ERR") {

        if (reply.size() >= 3)
            printf("Error [%s]: %s\n", reply[1].c_str(), reply[2].c_str());
        else
            printf("Error: invalid reply\n");

        return;
    }

    if (reply.size() == 1) {
        printf("(empty)\n");
        return;
    }

    for (size_t i = 1; i < reply.size(); i++)
        printf("%s\n", reply[i].c_str());
}

// Display available commands
void Client::showHelp() {

    printf("create_user <user_id> <password>\n");
    printf("login <user_id> <password>\n");

    printf("create_group <group_id>\n");
    printf("join_group <group_id>\n");
    printf("leave_group <group_id>\n");

    printf("list_groups\n");
    printf("list_requests <group_id>\n");
    printf("accept_request <group_id> <user_id>\n");

    printf("upload_file <group_id> <file_path>\n");
    printf("list_files <group_id>\n");

    printf("download_file <group_id> <file_name> <destination>\n");
    printf("show_downloads\n");

    printf("stop_share <group_id> <file_name>\n");

    printf("logout\n");
    printf("help\n");
    printf("quit\n");
}

// Logout user
void Client::logout() {

    if (token.empty()) {
        printf("Not logged in\n");
        return;
    }

    downloads.cancel();
    files.clear();

    vector<string> reply = sendCmd({"logout", token});

    showReply(reply);

    if (reply[0] == "OK")
        token.clear();

    if (reply.size() >= 2 && reply[0] == "ERR" && reply[1] == "AUTH") {
        token.clear();
    }
}

// Execute command
bool Client::runCmd(const vector<string>& args) {

    if (args.empty())
        return true;

    string cmd = args[0];

    // Commands that do not require login
    if (cmd == "quit" || cmd == "help" || cmd == "logout" || cmd == "show_downloads") {

        if (args.size() != 1) {
            printf("Error: this command takes no arguments\n");
            return true;
        }

        if (cmd == "quit")
            return false;

        if (cmd == "help")
            showHelp();
        else if (cmd == "logout")
            logout();
        else
            downloads.show();

        return true;
    }

    // Check required number of arguments
    size_t required = 0;

    if (cmd == "create_user" || cmd == "login" || cmd == "accept_request" || cmd == "upload_file" || cmd == "stop_share") {
        required = 3;
    }
    else if (cmd == "create_group" || cmd == "join_group" || cmd == "leave_group" || cmd == "list_requests" || cmd == "list_files") {
        required = 2;
    }
    else if (cmd == "list_groups") {
        required = 1;
    }
    else if (cmd == "download_file") {
        required = 4;
    }
    else {
        printf("Error: unknown command; type help\n");
        return true;
    }

    if (args.size() != required) {
        printf("Error: wrong argument count; type help\n");
        return true;
    }

    // Create user or login
    if (cmd == "create_user" || cmd == "login") {

        if (args[2].empty() || args[2].size() > 256) {
            printf("Error: password must have 1 to 256 characters\n");
            return true;
        }

        string passwordHash = Hash::digest(Net::pack({args[1], args[2]}));

        // Create new user
        if (cmd == "create_user") {
            showReply(sendCmd({"create_user", args[1], passwordHash}));
            return true;
        }

        // Already logged in
        if (!token.empty()) {
            printf("Error: logout before logging in again\n");
            return true;
        }

        // Generate login token
        string newToken = Net::getToken();

        vector<string> reply = sendCmd({"login", args[1], passwordHash, addr, newToken});

        if (reply.size() == 2 && reply[0] == "OK" && reply[1] == newToken) {
            token = newToken;
            printf("Logged in as %s\n", args[1].c_str());
        }
        else {
            showReply(reply);
        }

        return true;
    }

    // All remaining commands require login
    if (token.empty()) {
        printf("Error: login first\n");
        return true;
    }

    // Upload
    if (cmd == "upload_file") {
        upload(args[1], args[2]);
        return true;
    }

    // Download
    if (cmd == "download_file") {
        downloads.start(token, addr, args[1], args[2], args[3]);
        printf("Download started; use show_downloads\n");
        return true;
    }

    // Stop sharing
    if (cmd == "stop_share") {
        downloads.stopSharing(args[1], args[2]);
        files.remove(args[1], args[2]);
    }

    // Leaving a group cancels its downloads
    if (cmd == "leave_group")
        downloads.cancel(args[1]);

    // Build request for tracker
    vector<string> request = {cmd, token};

    for (size_t i = 1; i < args.size(); i++)
        request.push_back(args[i]);

    vector<string> reply = sendCmd(request);

    // Clear files after successfully leaving group
    if (cmd == "leave_group" && !reply.empty() && reply[0] == "OK") {
        files.clear(args[1]);
    }

    // Authentication failed
    if (reply.size() >= 2 && reply[0] == "ERR" && reply[1] == "AUTH") {
        downloads.cancel();
        files.clear();
        token.clear();
    }

    showReply(reply);

    return true;
}

// Start client
bool Client::run(const char* clientAddr, const char* cfgPath) {

    addr = clientAddr;

    // Validate address and initialize tracker connection
    if (!Net::validAddr(addr) || !link.init(cfgPath)) {
        fprintf(stderr, "Invalid client address or tracker config\n");
        return false;
    }

    // Start peer server
    if (!peer.start(addr)) {
        fprintf(stderr, "Cannot listen at client address\n");
        return false;
    }

    printf("Client %s. Type help for commands.\n", addr.c_str());

    string line;

    while (true) {

        printf("> ");
        fflush(stdout);

        if (!getline(cin, line))
            break;

        // Prevent very large commands
        if (line.size() > 4096) {
            printf("Error: command too long\n");
            continue;
        }

        try {

            vector<string> args;

            if (!parseLine(line, args)) {
                printf("Error: unclosed quote\n");
                continue;
            }

            if (!runCmd(args))
                break;
        }
        catch (const exception& e) {
            fprintf(stderr, "Client: %s\n", e.what());
        }
    }

    return true;
}