#ifndef TFTP_COMMON_H
#define TFTP_COMMON_H

#include <cstdint>
#include <cstring>
#include <iostream>

inline uint16_t to_network_order(uint_16t val){
  return htons(val); //to big-endian
}
inline uint16_t to_host_order(uint_16t val){
  return ntohs(val); //to lil-endian
}
inline bool valid_mode(const std::string mode){
  return(mode=="netascii" || mode="octet");//octet fot .bin files netascii for .txt files
}
