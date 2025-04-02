#ifndef TFTP_COMMON_HPP
#define TFTP_COMMON_HPP

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else // Linux/macOS
#include <arpa/inet.h>
#include <errno.h> // Include for errno
#include <fcntl.h> // Include for fcntl
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
const int INVALID_SOCKET = -1;
const int SOCKET_ERROR = -1;
#define closesocket close
#endif

// TFTP Opcodes
const uint16_t TFTP_OPCODE_RRQ = 1;
const uint16_t TFTP_OPCODE_WRQ = 2;
const uint16_t TFTP_OPCODE_DATA = 3;
const uint16_t TFTP_OPCODE_ACK = 4;
const uint16_t TFTP_OPCODE_ERROR = 5;

// TFTP Error Codes
const uint16_t TFTP_ERROR_NOT_DEFINED = 0;
const uint16_t TFTP_ERROR_FILE_NOT_FOUND = 1;
const uint16_t TFTP_ERROR_ACCESS_VIOLATION = 2;
const uint16_t TFTP_ERROR_DISK_FULL = 3;
const uint16_t TFTP_ERROR_ILLEGAL_OPERATION = 4;
const uint16_t TFTP_ERROR_UNKNOWN_TRANSFER_ID = 5;
const uint16_t TFTP_ERROR_FILE_ALREADY_EXISTS = 6;
const uint16_t TFTP_ERROR_NO_SUCH_USER = 7; // (not typically used)

// Constants
const int TFTP_DEFAULT_PORT = 69;
const int MAX_PACKET_SIZE = 516; // 4 byte header + 512 data
const int DATA_HEADER_SIZE = 4;
const int MAX_DATA_SIZE = 512;
const int ACK_PACKET_SIZE = 4;
const int ERROR_HEADER_SIZE = 4;
const int DEFAULT_TIMEOUT_SEC = 5; // Basic timeout in seconds

// Helper structure for network initialization/cleanup
struct NetworkInitializer {
  NetworkInitializer() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
#endif
  }
  ~NetworkInitializer() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

// Helper function to set socket timeout
inline bool set_socket_timeout(SOCKET sock, int seconds) {
#ifdef _WIN32
  DWORD timeout = seconds * 1000; // milliseconds
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                 sizeof(timeout)) == SOCKET_ERROR) {
    perror("setsockopt(SO_RCVTIMEO) failed");
    return false;
  }
#else
  struct timeval timeout;
  timeout.tv_sec = seconds;
  timeout.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) <
      0) {
    perror("setsockopt(SO_RCVTIMEO) failed");
    return false;
  }
#endif
  return true;
}

// --- Packet Creation Functions ---

inline std::vector<char> create_rrq_packet(const std::string &filename,
                                           const std::string &mode = "octet") {
  std::vector<char> packet;
  uint16_t opcode = htons(TFTP_OPCODE_RRQ);
  packet.insert(packet.end(), (char *)&opcode,
                (char *)&opcode + sizeof(opcode));
  packet.insert(packet.end(), filename.begin(), filename.end());
  packet.push_back('\0');
  packet.insert(packet.end(), mode.begin(), mode.end());
  packet.push_back('\0');
  return packet;
}

inline std::vector<char> create_wrq_packet(const std::string &filename,
                                           const std::string &mode = "octet") {
  std::vector<char> packet;
  uint16_t opcode = htons(TFTP_OPCODE_WRQ);
  packet.insert(packet.end(), (char *)&opcode,
                (char *)&opcode + sizeof(opcode));
  packet.insert(packet.end(), filename.begin(), filename.end());
  packet.push_back('\0');
  packet.insert(packet.end(), mode.begin(), mode.end());
  packet.push_back('\0');
  return packet;
}

inline std::vector<char>
create_data_packet(uint16_t block_num, const char *data, size_t data_size) {
  if (data_size > MAX_DATA_SIZE) {
    throw std::length_error("Data size exceeds maximum allowed");
  }
  std::vector<char> packet(DATA_HEADER_SIZE + data_size);
  uint16_t opcode = htons(TFTP_OPCODE_DATA);
  uint16_t net_block_num = htons(block_num);
  memcpy(packet.data(), &opcode, sizeof(opcode));
  memcpy(packet.data() + sizeof(opcode), &net_block_num, sizeof(net_block_num));
  if (data_size > 0) {
    memcpy(packet.data() + DATA_HEADER_SIZE, data, data_size);
  }
  return packet;
}

inline std::vector<char> create_ack_packet(uint16_t block_num) {
  std::vector<char> packet(ACK_PACKET_SIZE);
  uint16_t opcode = htons(TFTP_OPCODE_ACK);
  uint16_t net_block_num = htons(block_num);
  memcpy(packet.data(), &opcode, sizeof(opcode));
  memcpy(packet.data() + sizeof(opcode), &net_block_num, sizeof(net_block_num));
  return packet;
}

inline std::vector<char> create_error_packet(uint16_t error_code,
                                             const std::string &error_msg) {
  std::vector<char> packet;
  uint16_t opcode = htons(TFTP_OPCODE_ERROR);
  uint16_t net_error_code = htons(error_code);
  packet.insert(packet.end(), (char *)&opcode,
                (char *)&opcode + sizeof(opcode));
  packet.insert(packet.end(), (char *)&net_error_code,
                (char *)&net_error_code + sizeof(net_error_code));
  packet.insert(packet.end(), error_msg.begin(), error_msg.end());
  packet.push_back('\0');
  return packet;
}

// --- Packet Parsing Functions ---

inline uint16_t get_opcode(const char *buffer, size_t size) {
  if (size < 2)
    return 0; // Invalid packet
  uint16_t opcode;
  memcpy(&opcode, buffer, sizeof(opcode));
  return ntohs(opcode);
}

inline bool parse_ack_packet(const char *buffer, size_t size,
                             uint16_t &block_num) {
  if (size != ACK_PACKET_SIZE || get_opcode(buffer, size) != TFTP_OPCODE_ACK) {
    return false;
  }
  memcpy(&block_num, buffer + 2, sizeof(block_num));
  block_num = ntohs(block_num);
  return true;
}

inline bool parse_data_packet(const char *buffer, size_t size,
                              uint16_t &block_num, const char *&data_ptr,
                              size_t &data_size) {
  if (size < DATA_HEADER_SIZE || get_opcode(buffer, size) != TFTP_OPCODE_DATA) {
    return false;
  }
  memcpy(&block_num, buffer + 2, sizeof(block_num));
  block_num = ntohs(block_num);
  data_ptr = buffer + DATA_HEADER_SIZE;
  data_size = size - DATA_HEADER_SIZE;
  return true;
}

inline bool parse_error_packet(const char *buffer, size_t size,
                               uint16_t &error_code, std::string &error_msg) {
  if (size < ERROR_HEADER_SIZE + 1 ||
      get_opcode(buffer, size) !=
          TFTP_OPCODE_ERROR) { // Need at least header + null terminator
    return false;
  }
  memcpy(&error_code, buffer + 2, sizeof(error_code));
  error_code = ntohs(error_code);
  // Find the null terminator for the message
  const char *msg_start = buffer + ERROR_HEADER_SIZE;
  const char *msg_end =
      (const char *)memchr(msg_start, '\0', size - ERROR_HEADER_SIZE);
  if (msg_end == nullptr) { // Malformed error packet (missing null terminator)
    error_msg = "Malformed error packet received";
    return true; // Still parse code, but indicate issue with message
  }
  error_msg = std::string(msg_start, msg_end - msg_start);
  return true;
}

// Parses RRQ or WRQ
inline bool parse_request_packet(const char *buffer, size_t size,
                                 std::string &filename, std::string &mode) {
  uint16_t opcode = get_opcode(buffer, size);
  if (opcode != TFTP_OPCODE_RRQ && opcode != TFTP_OPCODE_WRQ) {
    return false;
  }
  if (size < 6)
    return false; // Opcode(2) + min_filename(1) + 0 + min_mode(1) + 0

  const char *ptr = buffer + 2;
  const char *end = buffer + size;

  // Find filename end (null terminator)
  const char *filename_end = (const char *)memchr(ptr, '\0', end - ptr);
  if (filename_end == nullptr ||
      filename_end == ptr) { // No filename or missing terminator
    return false;
  }
  filename = std::string(ptr, filename_end - ptr);
  ptr = filename_end + 1; // Move past null terminator

  if (ptr >= end)
    return false; // Packet ends abruptly after filename

  // Find mode end (null terminator)
  const char *mode_end = (const char *)memchr(ptr, '\0', end - ptr);
  if (mode_end == nullptr || mode_end == ptr) { // No mode or missing terminator
    return false;
  }
  mode = std::string(ptr, mode_end - ptr);

  // Basic check: mode should be something like "netascii" or "octet"
  std::string lower_mode = mode;
  for (char &c : lower_mode)
    c = tolower(c);
  if (lower_mode != "netascii" && lower_mode != "octet") {
    // Technically allowed by RFC, but we only support octet here
    // For strictness, could return false here.
    // Let's allow it but maybe warn later.
  }

  // Check if there's extra data after the mode's null terminator
  if (mode_end + 1 != end) {
    // There is extra data, which is technically not allowed by RFC 1350
    // but some implementations might send it. For strictness, return false.
    // return false;
  }

  return true;
}

#endif // TFTP_COMMON_HPP
