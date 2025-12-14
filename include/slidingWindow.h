#include <stdlib.h>
#include <stdint.h>
#include "sharedConstants.h"

typedef struct {
    uint32_t seqNum;
    uint8_t data[MAX_PAYLOAD_SIZE];
    int dataLen;
} Data;

int getLower();
uint32_t getLowestSeqNum();
int getLowestSeqNumData(uint8_t *data, uint32_t *seqNum);
void addDataToWindow(uint32_t sequenceNum, uint8_t *data, int dataLen);
int getDataFromWindow(uint32_t sequenceNum, uint8_t *data);
int isWindowOpen();
int isWindowClosed();
void updateLower(uint32_t seqNum);
void setupWindow(int windowSize);