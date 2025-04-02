## turbotftp

# Compile Server (needs C++17)
g++ src/tftp_server.cpp -o tftp_server -std=c++17 -lstdc++fs -pthread 

# Compile Client (needs C++17 for filesystem)
g++ src/tftp_client.cpp -o tftp_client -std=c++17
