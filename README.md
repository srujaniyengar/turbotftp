# TurboTFTP - High-Performance TFTP Client & Server

TurboTFTP is a lightweight and efficient Trivial File Transfer Protocol (TFTP) featured in C++ based on RFC 1350. It supports fast and reliable file transfers over UDP with RRQ (Read Request) and WRQ (Write Request) handling.
Features

- ✔ High-speed file transfers over UDP
- ✔ Supports file upload (WRQ) and download (RRQ)
- ✔ Implements 512-byte data packets with ACK handling
- ✔ Supports octet (binary) and netascii (text) modes
- ✔ Handles timeouts and retransmissions
- ✔ Lightweight and easy to integrate

## File Structure
```
📂 turboTFTP
│── 📂 src
│   ├── tftp_server.cpp   # Server implementation
│   ├── tftp_client.cpp   # Client implementation
│   ├── main_server.cpp   # Entry point for server
│   ├── main_client.cpp   # Entry point for client
│── 📂 include
│   ├── tftp_server.hpp   # Server class definition
│   ├── tftp_client.hpp   # Client class definition
│   ├── protocol.h        # TFTP packet structure
│   ├── tftp_common.h     # Utility functions
│── 📂 data
│   ├── test.txt          # Sample file for testing
│── 📂 build              # Compiled binaries
│── Makefile              # Build automation
│── README.md             # Documentation
```

## Build & Run
🔹 Build the Project
```bash
make
```
🔹 Start the Server
```bash
./build/tftp_server
```
🔹 Send a File (WRQ)
```make
./build/tftp_client --write file.txt
```
🔹 Request a File (RRQ)
```make
./build/tftp_client --read file.txt
```
