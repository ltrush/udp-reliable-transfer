#include <stdlib.h>
#include "slidingWindow.h"
#include <stdio.h>
#include <string.h>
#include "safeUtil.h"

static Data *window;
static int myWindowSize;
static int upper;
static int lower;
static int current;

void setupWindow(int windowSize)
{
    myWindowSize = windowSize;
    window = (Data *)sCalloc(windowSize, sizeof(Data));
    lower = 0;
    current = 0;
    upper = windowSize;
}

int isWindowOpen()
{
    return current != upper;
}

int isWindowClosed()
{
    return current == upper;
}

int getLower() {return lower;}

void updateLower(uint32_t seqNum)
{
    lower = seqNum;
    upper = lower + myWindowSize;
}

int getIndex(int seqNum) {return seqNum % myWindowSize;}

int getLowestSeqNumData(uint8_t *data, uint32_t *seqNum)
{
    int index = getIndex(lower);
    int dataLen = window[index].dataLen;
    *seqNum = window[index].seqNum;
    memcpy(data, window[index].data, dataLen);
    return dataLen;
}

uint32_t getLowestSeqNum()
{
    int index = getIndex(lower);
    return window[index].seqNum;
}

void addDataToWindow(uint32_t sequenceNum, uint8_t *data, int dataLen)
{
    if (isWindowClosed())
    {
        printf("error: adding PDU to window when window is closed");
        exit(1);
    }

    int index = getIndex(sequenceNum);
    memcpy(window[index].data, data, dataLen);
    window[index].dataLen = dataLen;
    window[index].seqNum = sequenceNum;
    current++;
}

int getDataFromWindow(uint32_t sequenceNum, uint8_t *data)
{
    int index = getIndex(sequenceNum);
    int dataLen = window[index].dataLen;
    memcpy(data, window[index].data, dataLen);
    return dataLen;
}