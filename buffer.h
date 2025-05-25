#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include "sharedConstants.h"

#define DATA_VALID 1
#define DATA_INVALID 0

typedef struct {
    uint32_t seqNum;
    uint8_t flag;
    uint8_t data[MAX_PAYLOAD_SIZE];
    int dataLen;
    int valid;
} Data;

void setupBuffer(int bufferSize);
void addDatatoBuffer(uint32_t seqNum, uint8_t flag, uint8_t * data, int dataLen);
int getDataFromBuffer(uint32_t seqNum, uint8_t * flag, uint8_t * data, int * dataLen);

#endif