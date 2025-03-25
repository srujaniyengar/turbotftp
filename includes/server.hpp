/*
 * RRQ see ra -> | Opcode (2 bytes) | Filename (N bytes) | NULL (1 byte) | Mode (N bytes) | NULL (1 byte) |
 * WRQ see ra -> | Opcode (2 bytes) | Filename (N bytes) | NULL (1 byte) | Mode (N bytes) | NULL (1 byte) |
*/

#ifndef TFTP_SERVER_HPP
#define TFTP_SERVER_HPP

#include <string>
#include <netinet/in.h>

#define SERVER_PORT 69      // Default UDP port
#define BUFFER_SIZE 516     //+ 4 bytes for header

class TFTPServer {
public:
    TFTPServer();
    ~TFTPServer();

    void start();

private:
    int sock;  
    // Handles incoming TFTP requests
    void handle_request();
    // Handles Read Request (RRQ) - Sending files
    void handle_rrq(struct sockaddr_in &client, socklen_t client_len, const std::string &filename);
    // Handles Write Request (WRQ) - Receiving files
    void handle_wrq(struct sockaddr_in &client, socklen_t client_len, const std::string &filename);
};

#endif 
