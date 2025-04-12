# turboTFTP - High-Performance TFTP Client & Server

 TurboTFTP is a lightweight and efficient Trivial File Transfer Protocol (TFTP) implementation in C++ based on RFC 1350. It supports fast and reliable file transfers over UDP with RRQ (Read Request) and WRQ (Write Request) handling.

## Features

-✔ High-speed file transfers over UDP

-✔ Supports file upload (WRQ) and download (RRQ)

-✔ Implements 512-byte data packets with ACK handling

-✔ Supports octet (binary) and netascii (text) modes

-✔ Handles timeouts and retransmissions

-✔ Lightweight and easy to integrate

## Build & Run

🔹 Compile the Server (C++17 required)
```cpp
g++ src/tftp_server.cpp -o tftp_server -std=c++17 -lstdc++fs -pthread
```
🔹 Compile the Client (C++17 required for filesystem)
```
g++ src/tftp_client.cpp -o tftp_client -std=c++17
```
🔹 Run the Server
```
./tftp_server
```
🔹 Send a File (WRQ)
```
./tftp_client <server> put <destination_file> <source_file>
```
🔹 Request a File (RRQ)
```
./tftp_client <server> get <source_file> <destination_file>
```

