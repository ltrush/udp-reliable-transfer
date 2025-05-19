#include <stdlib.h>
#include "connection.h"
#include <stdint.h>

#define CRC_GOOD 0

typedef struct header Header;

struct header
{
    uint32_t seqNum;
    uint16_t checksum;
    uint8_t flag;
};

void sendFlagOnly(const Connection * connection, int flag);
void retrieveHeader(uint8_t * pdu, int pduLen, uint8_t * flag, uint32_t * seqNum);
int createPDU(uint8_t * pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t * payload, int payloadLen);
void printPDU(uint8_t * aPDU, int pduLength);
int checkChecksum(uint8_t * aPDU, int pduLength);
