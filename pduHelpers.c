#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "checksum.h"
#include "safeUtil.h"
#include "sharedConstants.h"
#include "pduHelpers.h"

#define DEBUG

#define CHECKSUM_SIZE 2

int createPDU(uint8_t * pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t * payload, int payloadLen) {
    uint16_t checkSum = 0;
    int pduLength = sizeof(sequenceNumber) + sizeof(checkSum) + sizeof(flag) + payloadLen;

    uint32_t sequenceNumber_n = htonl(sequenceNumber);
    memcpy(pduBuffer, &sequenceNumber_n, sizeof(sequenceNumber));
    memcpy(pduBuffer + sizeof(sequenceNumber), &checkSum, sizeof(checkSum)); // checkSum is 0 for intitial calculation
    memcpy(pduBuffer + sizeof(sequenceNumber) + sizeof(checkSum), &flag, sizeof(flag));
    memcpy(pduBuffer + sizeof(sequenceNumber) + sizeof(checkSum) + sizeof(flag), payload, payloadLen);
    checkSum = in_cksum((unsigned short *)pduBuffer, pduLength);
    memcpy(pduBuffer + sizeof(sequenceNumber), &checkSum, sizeof(checkSum)); // put calculated checkSum in pdu
    return pduLength;
}

void sendFlagOnly(const Connection * connection, int flag) {
    uint8_t pdu[MAX_PDU_SIZE] = {0};
	int pduLen = 0;
    pduLen = createPDU(pdu, 0, flag, 0, 0); // no payload
	safeSendto(connection->socketNum, pdu, pduLen, 0, (struct sockaddr *) &connection->info, connection->addrLen);
}

// int32_t recv_buf(uint8_t * buf, int32_t len, int32_t serverSocketNum, Connection * connection, uint8_t * flag, uint32_t * seq_num)
// {
//     uint8_t pdu[MAX_PDU_SIZE];
//     int32_t recv_len = 0;
//     int32_t dataLen = 0;

//     recv_len = safeRecvfrom(serverSocketNum, pdu, len, connection);

//     dataLen = retrieveHeader(data_buf, recv_len, flag, seq_num);

//     // dataLen could be -1 if crc error or 0 if no data
//     if (dataLen > 0)
//         memcpy(buf, &data_buf[sizeof(Header)], dataLen);

//     return dataLen;
// }

void retrieveHeader(uint8_t * pdu, int pduLen, uint8_t * flag, uint32_t * seqNum) {
    Header * myHeader = (Header *) pdu;
    // int returnValue = 0;

    // if (in_cksum((unsigned short *) data_buf, recv_len) != 0) {
    //     returnValue = CRC_ERROR;
    // }
    // else {
        *flag = myHeader->flag;
        memcpy(seqNum, &(myHeader->seqNum), sizeof(myHeader->seqNum));
        *seqNum = ntohl(*seqNum);

        // returnValue = recv_len - sizeof(Header);
    // }

    // return returnValue;
}

int checkChecksum(uint8_t * aPDU, int pduLength) {
    uint16_t checkSum = in_cksum((unsigned short *)aPDU, pduLength);
    if (checkSum == 0) {
        #ifdef DEBUG
            printf("Checksum: Correct (0x%04x)\n", checkSum);
        #endif
        return CRC_GOOD;
    }
    else {
        #ifdef DEBUG
            printf("PDU corrupted: checksum is incorrect (0x%04x)\n", checkSum);
        #endif
        return CRC_ERROR;
    }
}

void printPDU(uint8_t * aPDU, int pduLength) {
    printf("-------- PDU Information --------\n\n");

    if (checkChecksum(aPDU, pduLength) == CRC_ERROR) return;

    uint32_t sequenceNumber_n = 0;
    memcpy(&sequenceNumber_n, aPDU, sizeof(sequenceNumber_n));
    uint32_t sequenceNumber_h = ntohl(sequenceNumber_n);
    printf("Sequence number: %d\n", sequenceNumber_h);

    uint8_t flag = 0;
    memcpy(&flag, aPDU + sizeof(sequenceNumber_n) + CHECKSUM_SIZE, sizeof(flag));
    printf("Flag: %d\n", flag);

    int payloadLen = pduLength - sizeof(sequenceNumber_n) - CHECKSUM_SIZE - sizeof(flag);
    printf("Payload length: %d\n", payloadLen);

    uint8_t payload[1400] = {0};
    memcpy(&payload, aPDU + sizeof(sequenceNumber_n) + CHECKSUM_SIZE + sizeof(flag), payloadLen);
    printf("Payload: %s\n\n", payload);
}
