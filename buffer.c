#include <stdlib.h>
#include "buffer.h"
#include <stdio.h>
#include <string.h>
#include "safeUtil.h"

static Data *buffer;
static int myBufferSize;

// bufferSize is how many entries in buffer
void setupBuffer(int bufferSize) {
    myBufferSize = bufferSize;
    buffer = (Data *)sCalloc(bufferSize, sizeof(Data));
    for (int i = 0; i < bufferSize; i++) {
        buffer[i].valid = 0;
    }
}

void addDatatoBuffer(uint32_t seqNum, uint8_t flag, uint8_t * data, int dataLen)
{
    int index = seqNum % myBufferSize;
    memcpy(buffer[index].data, data, dataLen);
    buffer[index].seqNum = seqNum;
    buffer[index].flag = flag;
    buffer[index].dataLen = dataLen;
    buffer[index].valid = DATA_VALID;
}

int getDataFromBuffer(uint32_t seqNum, uint8_t * flag, uint8_t * data, int * dataLen)
{
    int index = seqNum % myBufferSize;
    if (buffer[index].valid == DATA_INVALID) return DATA_INVALID;

    *dataLen = buffer[index].dataLen;
    *flag = buffer[index].flag;
    memcpy(data, buffer[index].data, *dataLen);
    buffer[index].valid = DATA_INVALID;
    return DATA_VALID;
}