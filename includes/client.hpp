/*
 * RRQ see ra -> | Opcode (2 bytes) | Filename (N bytes) | NULL (1 byte) | Mode (N bytes) | NULL (1 byte) |
 * WRQ see ra -> | Opcode (2 bytes) | Filename (N bytes) | NULL (1 byte) | Mode (N bytes) | NULL (1 byte) |
*/

#ifndef TFTP_CLIENT_HPP
#define TFTP_CLIENT_HPP

#include <string>
#include <netinet/in.h>

#define SERVER_PORT 69      // Default UDP server port
#define BUFFER_SIZE 516     // + 4-byte header

class TFTPClient {
public:
    TFTPClient(const std::string &server_ip);
    ~TFTPClient();

    // Send a Read Request (RRQ)
    void send_rrq(const std::string &filename);

    // Send a Write Request (WRQ)
    void send_wrq(const std::string &filename);

private:
    int sock;                 // UDP socket
    struct sockaddr_in server; // Server address
    // Handles receiving data from the server
    void receive_file(const std::string &filename);
    // Handles sending a file to the server
    void send_file(const std::string &filename);
};

#endif  // TFTP_CLIENT_HPP
