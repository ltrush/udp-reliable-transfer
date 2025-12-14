#include <stdlib.h>
#include "connection.h"
#include <stdint.h>

#define CRC_GOOD 0

int retrievePDU(Connection *connection, uint8_t *flag, uint32_t *seqNum, int *checksumResult, uint8_t *payload);
void sendPDU(const Connection *connection, uint8_t *payload, int payloadLen, int flag, int seqNum);
void sendFlagOnly(const Connection *connection, int flag, uint32_t seqNum);
int retrieveHeader(uint8_t *pdu, int pduLen, uint8_t *flag, uint32_t *seqNum, int *checksumResult);
int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen);
void printPDU(uint8_t *aPDU, int pduLength);
int checkChecksum(uint8_t *aPDU, int pduLength);
