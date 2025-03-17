#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

#define SIZE = 1024 //default size (dummy)
enum op_code{
  RREQ =1, //read req
  WREQ=2,  //write req
  DATA=3, //data
  ACK=4,
  ERROR=5 //err
};

typedef struct Packet{
  uint32_t op_code;
  uint32_t block;
  char data[SIZE]; //! note ra size is set to 1024 bytes by defaule 
}Packet; 

#endif 
