## turbotftp

# Compile Server (needs C++17)
g++ src/tftp_server.cpp -o tftp_server -std=c++17 -lstdc++fs -pthread 

# Compile Client (needs C++17 for filesystem)
g++ src/tftp_client.cpp -o tftp_client -std=c++17

# usage
server: `./tftp_server`
client: `./tftp_client host put destination_file source_file` `./tftp_client host get source_file destination_file`
