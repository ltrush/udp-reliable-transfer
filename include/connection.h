#ifndef CONNECTION_H
#define CONNECTION_H

#include <netinet/in.h>  
#include <sys/socket.h>

typedef struct {
    int socketNum;
    struct sockaddr_in6 info; // Supports 4 and 6 but requires IPv6 struct
    int addrLen;
} Connection;

#endif