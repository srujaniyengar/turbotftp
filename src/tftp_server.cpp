#include "tftp_common.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <thread> // Only used for a small delay, not true concurrency yet
#include <cstring> // For memcpy, memset
#include <filesystem> // Requires C++17 for path manipulation

// --- Forward Declarations ---
void handle_read_request(SOCKET transfer_sock, sockaddr_in client_addr, socklen_t client_addr_len, const std::string& filename);
void handle_write_request(SOCKET transfer_sock, sockaddr_in client_addr, socklen_t client_addr_len, const std::string& filename);


// --- Server Main Logic ---
void run_server(int port) {
    NetworkInitializer net_init; // RAII for Winsock init/cleanup

    SOCKET listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listen_sock == INVALID_SOCKET) {
        throw std::runtime_error("Failed to create listening socket");
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(listen_sock);
        #ifdef _WIN32
             throw std::runtime_error("Bind failed with error: " + std::to_string(WSAGetLastError()));
        #else
             throw std::runtime_error("Bind failed: " + std::string(strerror(errno)));
        #endif
    }

    std::cout << "TFTP Server listening on UDP port " << port << "..." << std::endl;

    char buffer[MAX_PACKET_SIZE];
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);

    while (true) {
        std::cout << "\nWaiting for new client request on port " << port << "..." << std::endl;
        int bytes_received = recvfrom(listen_sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len);

        if (bytes_received == SOCKET_ERROR) {
             std::cerr << "Warning: recvfrom failed on listen socket. Continuing..." << std::endl;
             #ifdef _WIN32
                 std::cerr << " WSError: " << WSAGetLastError() << std::endl;
             #else
                 perror(" recvfrom listen");
             #endif
            // In a real server, might need more robust error handling here.
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Avoid busy-loop on error
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Received request from " << client_ip << ":" << ntohs(client_addr.sin_port) << " (" << bytes_received << " bytes)" << std::endl;

        uint16_t opcode = get_opcode(buffer, bytes_received);
        std::string filename, mode;

        // Create a *new* socket for handling this transfer (uses an ephemeral port)
        SOCKET transfer_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (transfer_sock == INVALID_SOCKET) {
             std::cerr << "Error: Failed to create transfer socket. Ignoring request." << std::endl;
             // Optionally send ERROR packet back via listen_sock? Difficult without TID.
             continue;
        }
         // Set receive timeout for the transfer socket
        if (!set_socket_timeout(transfer_sock, DEFAULT_TIMEOUT_SEC)) {
            std::cerr << "Error: Failed to set timeout on transfer socket. Ignoring request." << std::endl;
            closesocket(transfer_sock);
            continue;
        }


        if (parse_request_packet(buffer, bytes_received, filename, mode)) {
             // **Security Note:** Basic filename validation. A real server needs MUCH more (sandbox, allowlists etc.)
             // Prevent path traversal (simple check)
             if (filename.find("..") != std::string::npos || filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
                 std::cerr << "Error: Invalid filename requested (potential path traversal): " << filename << std::endl;
                 std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ACCESS_VIOLATION, "Invalid filename characters");
                 // Send error back using the *transfer* socket (even though it hasn't received anything yet)
                 // The client expects the reply from a new port. Binding isn't strictly necessary here for sendto.
                 sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                 closesocket(transfer_sock); // Close the temporary socket
                 continue;
             }

             // Ensure we are using a safe base directory (current directory in this simple case)
             // Using std::filesystem for safer path handling (C++17)
             std::filesystem::path base_dir = std::filesystem::current_path();
             std::filesystem::path requested_path = base_dir / filename;

             // Canonical path helps resolve relative paths but doesn't fully prevent traversal on its own
             // std::filesystem::canonical might throw if the path doesn't exist (problematic for WRQ)
             // Let's just check if the resulting path starts with the base directory.
             // Note: This check is still basic and might be bypassable on some systems.
              try {
                 auto abs_requested = std::filesystem::absolute(requested_path);
                 auto abs_base = std::filesystem::absolute(base_dir);
                 if (abs_requested.string().rfind(abs_base.string(), 0) != 0) {
                      std::cerr << "Error: Filename attempts to access outside base directory: " << filename << std::endl;
                     std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ACCESS_VIOLATION, "Access denied");
                     sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                     closesocket(transfer_sock);
                     continue;
                 }
             } catch (const std::filesystem::filesystem_error& fs_err) {
                 std::cerr << "Error processing path: " << fs_err.what() << std::endl;
                 std::vector<char> error_packet = create_error_packet(TFTP_ERROR_NOT_DEFINED, "Internal server error processing path");
                 sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                 closesocket(transfer_sock);
                 continue;
             }


             std::cout << "  Request Type: " << (opcode == TFTP_OPCODE_RRQ ? "RRQ" : "WRQ") << std::endl;
             std::cout << "  Filename: " << filename << std::endl;
             std::cout << "  Mode: " << mode << std::endl;

            // We only support octet mode in this simple server
            std::string lower_mode = mode;
            for(char &c : lower_mode) c = tolower(c);
            if (lower_mode != "octet") {
                 std::cerr << "Error: Unsupported mode requested: " << mode << ". Only 'octet' is supported." << std::endl;
                 std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ILLEGAL_OPERATION, "Unsupported mode (use octet)");
                  sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                  closesocket(transfer_sock); // Close the temporary socket
                 continue;
            }


            // --- Handle Request (using the new transfer socket) ---
            // NOTE: This simple server handles one transfer at a time sequentially.
            // A real server would typically spawn a new thread or process here.
             try {
                if (opcode == TFTP_OPCODE_RRQ) {
                     handle_read_request(transfer_sock, client_addr, client_addr_len, filename);
                 } else { // WRQ
                     handle_write_request(transfer_sock, client_addr, client_addr_len, filename);
                 }
             } catch (const std::exception& e) {
                 std::cerr << "Error during transfer: " << e.what() << std::endl;
                 // Error packet might have already been sent by handler, or maybe not.
                 // Ensure socket is closed if an exception occurred during handling.
             }
            // Transfer socket should be closed by the handler function upon completion or error.
            // closesocket(transfer_sock); // Ensure closed (though handlers should do it)

        } else {
            std::cerr << "Error: Received invalid or malformed request packet." << std::endl;
            // Send ERROR (Illegal TFTP Operation) back to the sender using the *listen* socket? Risky.
             // Better to just drop invalid packets received on port 69.
            // Or send from the temporary transfer_sock before closing it.
             std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ILLEGAL_OPERATION, "Malformed request packet");
             sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
             closesocket(transfer_sock);
        }
    } // end while(true)

    closesocket(listen_sock); // Should not be reached in this simple loop
}

// --- Handler Functions ---

void handle_read_request(SOCKET transfer_sock, sockaddr_in client_addr, socklen_t client_addr_len, const std::string& filename) {
    std::cout << "Handling RRQ for " << filename << std::endl;

    std::ifstream input_file(filename, std::ios::binary);
    if (!input_file) {
        std::cerr << "Error: File not found or cannot open: " << filename << std::endl;
        std::vector<char> error_packet = create_error_packet(TFTP_ERROR_FILE_NOT_FOUND, "File not found");
        sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
        closesocket(transfer_sock);
        return;
    }

    uint16_t block_num = 1;
    char data_buffer[MAX_DATA_SIZE];
    size_t bytes_read;
    bool last_ack_received = false;
    char ack_buffer[MAX_PACKET_SIZE]; // Buffer for receiving ACKs


    do {
        input_file.read(data_buffer, MAX_DATA_SIZE);
        bytes_read = input_file.gcount();
       // std::cout << "Read " << bytes_read << " bytes for block " << block_num << std::endl;

        std::vector<char> data_packet = create_data_packet(block_num, data_buffer, bytes_read);

        // Send DATA packet
        //std::cout << "Sending DATA block " << block_num << " (" << bytes_read << " bytes)..." << std::endl;
        if (sendto(transfer_sock, data_packet.data(), data_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len) == SOCKET_ERROR) {
            std::cerr << "Error: sendto failed (DATA block " << block_num << ")" << std::endl;
            closesocket(transfer_sock);
            throw std::runtime_error("Send failed during RRQ.");
        }

        // Wait for ACK for this block
        //std::cout << "Waiting for ACK " << block_num << "..." << std::endl;
        int retry_count = 0;
        const int MAX_RETRIES = 5; // Example retry limit

        while(retry_count < MAX_RETRIES) {
            int bytes_received = recvfrom(transfer_sock, ack_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len); // Check source addr?

            if (bytes_received == SOCKET_ERROR) {
                #ifdef _WIN32
                    int error = WSAGetLastError();
                    if (error == WSAETIMEDOUT) {
                         std::cerr << "Warning: Timeout waiting for ACK " << block_num << ". Retrying send (attempt " << retry_count + 1 << ")" << std::endl;
                         retry_count++;
                         // Resend last data packet
                         if (sendto(transfer_sock, data_packet.data(), data_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len) == SOCKET_ERROR) {
                            std::cerr << "Error: sendto failed on retry (DATA block " << block_num << ")" << std::endl;
                            closesocket(transfer_sock);
                            throw std::runtime_error("Send failed during RRQ retry.");
                         }
                         continue; // Go back to recvfrom
                    } else {
                        std::cerr << "Error: recvfrom failed waiting for ACK " << block_num << " with error code " << error << std::endl;
                         closesocket(transfer_sock);
                        throw std::runtime_error("Receive failed waiting for ACK.");
                    }
                #else
                     if (errno == EAGAIN || errno == EWOULDBLOCK) {
                         std::cerr << "Warning: Timeout waiting for ACK " << block_num << ". Retrying send (attempt " << retry_count + 1 << ")" << std::endl;
                         retry_count++;
                          // Resend last data packet
                         if (sendto(transfer_sock, data_packet.data(), data_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len) == SOCKET_ERROR) {
                             perror("sendto failed on retry");
                             closesocket(transfer_sock);
                             throw std::runtime_error("Send failed during RRQ retry.");
                         }
                         continue; // Go back to recvfrom
                     } else {
                        perror("recvfrom failed waiting for ACK");
                        closesocket(transfer_sock);
                        throw std::runtime_error("Receive failed waiting for ACK.");
                     }
                #endif
            }

             // Optional: Add check here: is the packet from the expected client_addr?
             // if (received_client_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || received_client_addr.sin_port != client_addr.sin_port) { ... }

            uint16_t opcode = get_opcode(ack_buffer, bytes_received);
            if (opcode == TFTP_OPCODE_ACK) {
                 uint16_t acked_block_num;
                 if (parse_ack_packet(ack_buffer, bytes_received, acked_block_num)) {
                     if (acked_block_num == block_num) {
                        //std::cout << "Received ACK for block " << acked_block_num << std::endl;
                         last_ack_received = true; // Correct ACK received
                         break; // Exit retry loop
                     } else if (acked_block_num < block_num) {
                         // Old ACK, ignore it and continue waiting for the correct one
                          std::cout << "Received old ACK " << acked_block_num << ". Ignoring and waiting." << std::endl;
                          // Do not increment retry count here, just wait again
                          continue; // Go back to recvfrom immediately
                     } else {
                         // ACK for future block? Error.
                         std::cerr << "Error: Received ACK for future block " << acked_block_num << ". Aborting." << std::endl;
                         // Send ERROR?
                         closesocket(transfer_sock);
                         throw std::runtime_error("Protocol error: Unexpected ACK number.");
                     }
                 } else {
                      std::cerr << "Warning: Received malformed ACK packet. Ignoring and waiting." << std::endl;
                       // Treat as timeout? Or just ignore? Let's ignore and wait.
                       continue; // Go back to recvfrom
                 }
            } else if (opcode == TFTP_OPCODE_ERROR) {
                 uint16_t error_code;
                 std::string error_msg;
                 parse_error_packet(ack_buffer, bytes_received, error_code, error_msg); // Ignore parse result, report what we can
                 std::cerr << "Error: Received TFTP Error from client during RRQ: Code " << error_code << ": " << error_msg << ". Aborting transfer." << std::endl;
                 closesocket(transfer_sock);
                 return; // Abort transfer cleanly
            } else {
                 // Unexpected packet type
                 std::cerr << "Warning: Received unexpected packet type (Opcode: " << opcode << ") while waiting for ACK " << block_num << ". Ignoring." << std::endl;
                 // Send ERROR (Illegal Operation)? Maybe just ignore and wait.
                 continue; // Go back to recvfrom
            }
        } // End retry while loop

         if (!last_ack_received) {
            // Exited loop because MAX_RETRIES reached
            std::cerr << "Error: Max retries exceeded waiting for ACK " << block_num << ". Aborting transfer." << std::endl;
             closesocket(transfer_sock);
             throw std::runtime_error("Transfer aborted due to timeout.");
         }


        block_num++; // Increment block number for next packet

    } while (bytes_read == MAX_DATA_SIZE); // Continue if the last packet was full

    std::cout << "RRQ for " << filename << " completed successfully." << std::endl;
    closesocket(transfer_sock);
}


void handle_write_request(SOCKET transfer_sock, sockaddr_in client_addr, socklen_t client_addr_len, const std::string& filename) {
     std::cout << "Handling WRQ for " << filename << std::endl;

     // Security Check: Does file already exist? (Optional, based on server policy)
     // RFC 1350 doesn't strictly forbid overwriting, but it's often desired.
     std::filesystem::path filepath = filename;
     if (std::filesystem::exists(filepath)) {
         std::cerr << "Error: File already exists: " << filename << std::endl;
         std::vector<char> error_packet = create_error_packet(TFTP_ERROR_FILE_ALREADY_EXISTS, "File already exists");
         sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
         closesocket(transfer_sock);
         return;
     }


    std::ofstream output_file(filename, std::ios::binary | std::ios::trunc);
    if (!output_file) {
        std::cerr << "Error: Cannot create or open file for writing: " << filename << std::endl;
        // Determine if it's access violation or disk full? Hard to know for sure. Use Access Violation.
        std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ACCESS_VIOLATION, "Cannot write file");
        sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
        closesocket(transfer_sock);
        return;
    }

    // Send ACK 0 to start the transfer
    std::cout << "Sending ACK 0 to client..." << std::endl;
    std::vector<char> ack0_packet = create_ack_packet(0);
    if (sendto(transfer_sock, ack0_packet.data(), ack0_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len) == SOCKET_ERROR) {
        std::cerr << "Error: sendto failed (ACK 0)" << std::endl;
        output_file.close();
        remove(filename.c_str()); // Clean up potentially empty file
        closesocket(transfer_sock);
        throw std::runtime_error("Send failed (ACK 0).");
    }

    uint16_t expected_block_num = 1;
    char data_buffer[MAX_PACKET_SIZE]; // Buffer for receiving DATA
    bool transfer_complete = false;

    while (!transfer_complete) {
        //std::cout << "Waiting for DATA block " << expected_block_num << "..." << std::endl;
        int bytes_received = recvfrom(transfer_sock, data_buffer, MAX_PACKET_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len); // Check source addr?

        if (bytes_received == SOCKET_ERROR) {
             #ifdef _WIN32
                 int error = WSAGetLastError();
                 if (error == WSAETIMEDOUT) {
                     std::cerr << "Error: Timeout waiting for DATA block " << expected_block_num << ". Aborting." << std::endl;
                     // Server usually doesn't resend ACKs on timeout, just aborts.
                 } else {
                     std::cerr << "Error: recvfrom failed waiting for DATA " << expected_block_num << " with error code " << error << std::endl;
                 }
             #else
                 if (errno == EAGAIN || errno == EWOULDBLOCK) {
                      std::cerr << "Error: Timeout waiting for DATA block " << expected_block_num << ". Aborting." << std::endl;
                 } else {
                     perror("recvfrom failed waiting for DATA");
                 }
             #endif
              output_file.close();
              remove(filename.c_str()); // Clean up partial file
              closesocket(transfer_sock);
              throw std::runtime_error("Receive failed or timed out waiting for DATA.");
        }

         // Optional: Add check here: is the packet from the expected client_addr?

         uint16_t opcode = get_opcode(data_buffer, bytes_received);

         if (opcode == TFTP_OPCODE_DATA) {
             uint16_t block_num;
             const char* data_ptr;
             size_t data_size;

             if (!parse_data_packet(data_buffer, bytes_received, block_num, data_ptr, data_size)) {
                  std::cerr << "Error: Received malformed DATA packet. Sending error." << std::endl;
                  std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ILLEGAL_OPERATION, "Malformed DATA packet");
                  sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                  output_file.close();
                  remove(filename.c_str());
                  closesocket(transfer_sock);
                  throw std::runtime_error("Malformed DATA received.");
             }

            // std::cout << "Received DATA block " << block_num << " (" << data_size << " bytes)" << std::endl;

            if (block_num == expected_block_num) {
                 output_file.write(data_ptr, data_size);
                 if (!output_file) {
                      std::cerr << "Error: Failed to write to local file (disk full?)." << std::endl;
                      std::vector<char> error_packet = create_error_packet(TFTP_ERROR_DISK_FULL, "Disk full or write error");
                      sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                      output_file.close();
                      remove(filename.c_str());
                      closesocket(transfer_sock);
                      throw std::runtime_error("File write error during WRQ.");
                 }

                 // Send ACK for the received block
                 std::vector<char> ack_packet = create_ack_packet(block_num);
                 if (sendto(transfer_sock, ack_packet.data(), ack_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len) == SOCKET_ERROR) {
                      output_file.close();
                      remove(filename.c_str());
                      closesocket(transfer_sock);
                      throw std::runtime_error("sendto failed (ACK)");
                 }
                // std::cout << "Sent ACK for block " << block_num << std::endl;

                 // Check for end of transfer
                 if (data_size < MAX_DATA_SIZE) {
                     transfer_complete = true;
                     std::cout << "WRQ transfer complete. Received " << expected_block_num << " blocks." << std::endl;
                 } else {
                     expected_block_num++; // Move to next block
                 }
            } else if (block_num < expected_block_num) {
                 // Received old block, re-send ACK for it (handle duplicates)
                 std::cout << "Received duplicate DATA block " << block_num << ". Resending ACK " << block_num << "." << std::endl;
                 std::vector<char> ack_packet = create_ack_packet(block_num);
                 sendto(transfer_sock, ack_packet.data(), ack_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len); // Ignore send errors here?
            } else {
                 // Received block number higher than expected - protocol error.
                 std::cerr << "Error: Received unexpected DATA block " << block_num << " (expected " << expected_block_num << "). Sending error." << std::endl;
                 std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ILLEGAL_OPERATION, "Unexpected block number");
                 sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
                 output_file.close();
                 remove(filename.c_str());
                 closesocket(transfer_sock);
                 throw std::runtime_error("Protocol error: Unexpected block number.");
            }

         } else if (opcode == TFTP_OPCODE_ERROR) { // Client might send error
             uint16_t error_code;
             std::string error_msg;
             parse_error_packet(data_buffer, bytes_received, error_code, error_msg);
             std::cerr << "Error: Received TFTP Error from client during WRQ: Code " << error_code << ": " << error_msg << ". Aborting transfer." << std::endl;
             output_file.close();
             remove(filename.c_str());
             closesocket(transfer_sock);
             return; // Abort transfer
         }
         else {
              // Unexpected packet type
             std::cerr << "Error: Received unexpected packet type (Opcode: " << opcode << ") while waiting for DATA " << expected_block_num << ". Sending error." << std::endl;
             std::vector<char> error_packet = create_error_packet(TFTP_ERROR_ILLEGAL_OPERATION, "Unexpected packet type");
             sendto(transfer_sock, error_packet.data(), error_packet.size(), 0, (struct sockaddr*)&client_addr, client_addr_len);
             output_file.close();
             remove(filename.c_str());
             closesocket(transfer_sock);
             throw std::runtime_error("Protocol error: Unexpected packet type.");
         }

    } // end while(!transfer_complete)


    output_file.close(); // Close the file successfully
    std::cout << "WRQ for " << filename << " completed successfully." << std::endl;
    closesocket(transfer_sock);

}


// --- Main Function ---
int main(int argc, char* argv[]) {
    int port = TFTP_DEFAULT_PORT;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
            if (port <= 0 || port > 65535) {
                std::cerr << "Invalid port number: " << argv[1] << ". Using default " << TFTP_DEFAULT_PORT << std::endl;
                port = TFTP_DEFAULT_PORT;
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid port argument '" << argv[1] << "'. Using default " << TFTP_DEFAULT_PORT << ". Error: " << e.what() << std::endl;
            port = TFTP_DEFAULT_PORT;
        }
    }

    try {
        run_server(port);
    } catch (const std::exception& e) {
        std::cerr << "Server failed: " << e.what() << std::endl;
        return 1;
    }

    return 0; // Should not be reached
}
