#ifndef CONNECTION_H
#define CONNECTION_H

#include <netinet/in.h>  
#include <sys/socket.h>

typedef struct {
    int socketNum;
    struct sockaddr_in6 info;
    socklen_t addrLen;
} Connection;

#endif