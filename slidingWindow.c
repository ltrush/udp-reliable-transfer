#include <stdlib.h>
#include "slidingWindow.h"
#include "safeUtil.h"

static struct PDU * window;
int myWindowSize;
int upper;
int lower;
int current;

void setupWindow(int windowSize, int pduSize) {
    myWindowSize = windowSize;
    window = (struct PDU *)sCalloc(myWindowSize, pduSize);
    lower = 0;
    current = 0;
    upper = windowSize;
}

int isWindowOpen() {
    return current != upper;
}

int isWindowClosed() {
    return current == upper;
}

void addPDU(int sequenceNum, uint8_t * pdu, int pduLen) {
    if (isWindowClosed()) {
        printf("error: adding PDU to window when window is closed");
        exit(1);
    }

    int index = sequenceNum % myWindowSize;
    memcpy(&window[index], pdu, pduLen);
    current++;
}

uint8_t * getPDU(int sequenceNum) {
    int index = sequenceNum % myWindowSize;
    return &window[sequenceNum].data;
}