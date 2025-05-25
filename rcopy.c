// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "buffer.h"
#include "pollLib.h"
#include "cpe464.h"
#include "sharedConstants.h"
#include "pduHelpers.h"
#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"

#define DEBUG
#define MAX_REMOTE_MACHINE_NAME_LEN 1000 // arbitrary

typedef struct Parameters{ // IS THIS OKAY TO HAVE IN HERE? or should be in header file
	char fromFilename[MAX_FILENAME_LENGTH + 1]; // includes null
	int fromFilenameLen;
	char toFilename[MAX_FILENAME_LENGTH + 1]; // includes null
	int toFilenameLen;
	int windowSize;
	int bufferSize;
	double errorRate;
	char remoteMachine[MAX_REMOTE_MACHINE_NAME_LEN];
	int portNumber;
} Parameters;

typedef enum State STATE;
enum State {
	SETUP_CONNECTION, SEND_FILENAME, FILENAME_STATUS, FILENAME_OK, RECV_DATA, SEND_EOF_ACK, DONE
};

typedef enum DataState DATA_STATE;
enum DataState {
	IN_ORDER, BUFFER, FLUSH, DATA_STATE_DONE
};

STATE setupConnection(Parameters * myParameters, Connection * server);
STATE sendFilename(Parameters * myParameters, Connection * server, uint32_t * clientSeqNum);
STATE getFilenameStatus(Parameters * myParameters, Connection * server);
STATE filenameOk(Connection * server, uint32_t * clientSeqNum);
STATE receiveData(Connection * server, uint32_t * clientSeqNum);
DATA_STATE inOrder(Connection * server, uint32_t * clientSeqNum, uint32_t * expectedSeqNum, uint32_t receivedSeqNum, uint32_t * highestRecvSeqNum, uint8_t * payload, int payloadLen, uint8_t flag);
DATA_STATE buffer(Connection * server, uint32_t * clientSeqNum, uint32_t * expectedSeqNum, uint32_t receivedSeqNum, uint32_t * highestRecvSeqNum, uint8_t * payload, int payloadLen, uint8_t flag);
DATA_STATE flush(Connection * server, uint32_t * expectedSeqNum, uint32_t * highestRecvSeqNum, uint32_t * clientSeqNum);
STATE sendEOFAck(Connection * server, uint32_t * clientSeqNum);

void checkArgs(int argc, char * argv[], Parameters * myParameters);
void processFile(Parameters * myParameters);
void sendFilenamePDU(Parameters * myParameters, int clientSeqNum, Connection * server);
void sendSREJ(Connection * server, uint32_t *clientSeqNum, uint32_t missingSeqNum);
void sendRR(Connection * server, uint32_t *clientSeqNum, uint32_t expectedSeqNum);

int outputFileDesc;

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	Parameters myParameters = {
		.fromFilename = {0},
		.fromFilenameLen = 0,
		.toFilename = {0},
		.toFilenameLen = 0,
		.windowSize = 0,
		.bufferSize = 0,
		.errorRate = 0.0,
		.remoteMachine = {0},
		.portNumber = 0
	};
	
	checkArgs(argc, argv, &myParameters);

	sendErr_init(myParameters.errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

	processFile(&myParameters);
	
	close(socketNum);

	return 0;
}

void processFile(Parameters * myParameters) {
	Connection server = {
		.socketNum = 0,
		.info = {0},
		.addrLen = sizeof(struct sockaddr_in6)
	};
	setupPollSet();
	setupBuffer(myParameters->windowSize);
	outputFileDesc = 0;
	uint32_t clientSeqNum = 0;
	STATE state = SETUP_CONNECTION;

	while (state != DONE) {
		switch (state)
		{
			case SETUP_CONNECTION:
				#ifdef DEBUG
					printf("\nIn SETUP_CONNECTION state\n");
				#endif
				state = setupConnection(myParameters, &server);
				break;

			case SEND_FILENAME:
				#ifdef DEBUG
					printf("\nIn SEND_FILENAME state\n");
				#endif
				state = sendFilename(myParameters, &server, &clientSeqNum);
				break;

			case FILENAME_STATUS:
				#ifdef DEBUG
					printf("\nIn FILENAME_STATUS state\n");
				#endif
				state = getFilenameStatus(myParameters, &server);
				break;
			
			case FILENAME_OK:
				#ifdef DEBUG
					printf("\nIn FILENAME_OK state\n");
				#endif
				state = filenameOk(&server, &clientSeqNum);
				break;

			case RECV_DATA:
				#ifdef DEBUG
					printf("\nIn RECV_DATA state\n");
				#endif
				state = receiveData(&server, &clientSeqNum);
				break;

			case SEND_EOF_ACK:
				#ifdef DEBUG
					printf("\nIn SEND_EOF_ACK state\n");
				#endif
				state = sendEOFAck(&server, &clientSeqNum);
				break;

			case DONE:
				break;

			default:
				#ifdef DEBUG
					printf("ERROR - in default state\n");
				#endif
				break;
		}
	}
}

// close old socket if necessary, setup new socket, add socket to poll set, setup connection 
STATE setupConnection(Parameters * myParameters, Connection * server)
{
    STATE nextState = SEND_FILENAME;

    // if we have connected to server before, close it before reconnect
    if (server->socketNum > 0)
    {
        close(server->socketNum);
		removeFromPollSet(server->socketNum);
    }

	server->socketNum = setupUdpClientToServer(&server->info, myParameters->remoteMachine, myParameters->portNumber);
	addToPollSet(server->socketNum);
	#ifdef DEBUG
		printf("Will be using socket %d to communicate with server\n", server->socketNum);
	#endif

    return nextState;
}

// send filename, get response
// return SETUP_CONNECTION if no reply from server, DONE if bad filename, FILE_OK otherwise
STATE sendFilename(Parameters * myParameters, Connection * server, uint32_t * clientSeqNum) {
	int nextState = DONE;
	static int retryCount = 0;

	sendFilenamePDU(myParameters, *clientSeqNum, server);
	(*clientSeqNum)++;

	// need to make a marco for the 10
	nextState = processPoll(server, &retryCount, SHORT_TIME, 10, SETUP_CONNECTION, FILENAME_STATUS, DONE);

	return(nextState);
}

// gets filenameStatus, checks checksum, checks flag, sends FNAME_OK_ACK and opens to-file if appropriate
// return SETUP_CONNECTION if checksum bad or flag unknown, DONE if FNAME_BAD, RECV_DATA if FILENAME_OK
STATE getFilenameStatus(Parameters * myParameters, Connection * server)
{
	STATE nextState = DONE;
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	int checksumResult;
	uint8_t payload[MAX_PAYLOAD_SIZE] = {0};
	retrievePDU(server, &flag, &seqNum, &checksumResult, payload);
	uint8_t filenameStatus = payload[0];

// one is fix this to retrievePDu and two is to make it so that we poll for 1 second 10times after sending file ok ack while we wait for first data,
	if (checksumResult == CRC_ERROR)
	{
		#ifdef DEBUG
			printf("Filename status PDU corrupted\n");
		#endif
		nextState = SETUP_CONNECTION;
	} else if (filenameStatus == FNAME_BAD) {
		printf("File \"%s\" not found by server\n", myParameters->fromFilename);
		nextState = DONE;
	} else if (filenameStatus == FNAME_OK) {
		#ifdef DEBUG
			printf("File \"%s\" found by server\n", myParameters->fromFilename);
		#endif
		
		if ((outputFileDesc = open(myParameters->toFilename, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
			perror("File open error: ");
			nextState = DONE;
		} else {
			nextState = FILENAME_OK;
		}	
	} else nextState = SETUP_CONNECTION;

    return nextState;
}

STATE filenameOk(Connection * server, uint32_t * clientSeqNum) 
{
	STATE nextState = FILENAME_OK;
	static int retryCount = 0;

	sendFlagOnly(server, FNAME_OK_ACK, *clientSeqNum);
	(*clientSeqNum)++;
	nextState = processPoll(server, &retryCount, SHORT_TIME, 10, FILENAME_OK, RECV_DATA, DONE);
	
	return nextState;
}

STATE receiveData(Connection * server, uint32_t * clientSeqNum)
{
	static DATA_STATE dataState = IN_ORDER;
    uint32_t receivedSeqNum = 0;
	uint8_t flag = 0;
	int checksumResult;
	uint8_t pdu[MAX_PDU_SIZE] = {0};
    uint8_t payload[MAX_PAYLOAD_SIZE] = {0};
    static uint32_t expectedSeqNum = START_SEQ_NUM;
	uint32_t highestSeqNum = expectedSeqNum;

	if (pollCall(LONG_TIME) == 0)
    {
		#ifdef DEBUG
        	printf("Timeout after 10 seconds, server must be gone.\n");
		#endif
        return DONE;
    }

	int pduLen = safeRecvfrom(server->socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *) &server->info, &server->addrLen);
	int payloadLen = retrieveHeader(pdu, pduLen, &flag, &receivedSeqNum, &checksumResult);
	memcpy(payload, pdu + PDU_HEADER_SIZE, payloadLen) ;
	
	if (checksumResult == CRC_ERROR)
	{
		#ifdef DEBUG
			printf("Data with sequence # %d corrupted. Dropping.\n", receivedSeqNum);
		#endif
		return RECV_DATA;
	}

	#ifdef DEBUG
		printf("Expected seqNum #%d, got seqNum #%d\n", expectedSeqNum, receivedSeqNum);
		// printf("Data: \"%s\"\n", payload);
	#endif

	switch (dataState) {
		case IN_ORDER:
			#ifdef DEBUG
				printf("\nRECV_DATA: IN_ORDER state\n");
			#endif
			dataState = inOrder(server, clientSeqNum, &expectedSeqNum, receivedSeqNum, &highestSeqNum, payload, payloadLen, flag);
			break;
		case BUFFER:
			#ifdef DEBUG
				printf("\nRECV_DATA: BUFFER state\n");
			#endif
			dataState = buffer(server, clientSeqNum, &expectedSeqNum, receivedSeqNum, &highestSeqNum, payload, payloadLen, flag);
			break;
		default:
			printf("ERROR - in default of RECV_DATA state\n");
			break;
	}

	if (dataState == FLUSH) {
		#ifdef DEBUG
			printf("\nRECV_DATA: FLUSH state\n");
		#endif		
		dataState = flush(server, &expectedSeqNum, &highestSeqNum, clientSeqNum);
	}

	if (dataState == DATA_STATE_DONE) return SEND_EOF_ACK;
	else return RECV_DATA;
}

DATA_STATE inOrder(Connection * server, uint32_t * clientSeqNum, uint32_t * expectedSeqNum, uint32_t receivedSeqNum, uint32_t * highestRecvSeqNum, uint8_t * payload, int payloadLen, uint8_t flag) {
	DATA_STATE nextDataState = IN_ORDER;
	
	if (receivedSeqNum == *expectedSeqNum) {
		// write data and send RR for next seq #
		if (flag == END_OF_FILE) return DATA_STATE_DONE;
		*highestRecvSeqNum = receivedSeqNum;
		(*expectedSeqNum)++;
		write(outputFileDesc, payload, payloadLen);
		sendRR(server, clientSeqNum, *expectedSeqNum);
		nextDataState = IN_ORDER;
	} else if (receivedSeqNum > *expectedSeqNum) {
		// buffer data and send SREJ for missing seq #
		*highestRecvSeqNum = receivedSeqNum;
		addDatatoBuffer(receivedSeqNum, flag, payload, payloadLen);
		printf("added data to buffer\n");
		sendSREJ(server, clientSeqNum, *expectedSeqNum);
		nextDataState = BUFFER;
	} else sendRR(server, clientSeqNum, *expectedSeqNum);

	return nextDataState;
}

DATA_STATE buffer(Connection * server, uint32_t * clientSeqNum, uint32_t * expectedSeqNum, uint32_t receivedSeqNum, uint32_t * highestRecvSeqNum, uint8_t * payload, int payloadLen, uint8_t flag) {
	DATA_STATE nextDataState = BUFFER;

	if (receivedSeqNum == *expectedSeqNum) {
		// write data
		if (flag == END_OF_FILE) return DATA_STATE_DONE;
		#ifdef DEBUG
			printf("Writing received data, ending buffering, going to flush\n");
		#endif
		(*expectedSeqNum)++;
		write(outputFileDesc, payload, payloadLen);
		nextDataState = FLUSH;
	} else if (receivedSeqNum > *expectedSeqNum) {
		// store data
		#ifdef DEBUG
			printf("Buffering received data\n");
		#endif
		*highestRecvSeqNum = receivedSeqNum;
		addDatatoBuffer(receivedSeqNum, flag, payload, payloadLen);
		nextDataState = BUFFER;
	} else sendRR(server, clientSeqNum, *expectedSeqNum);
	
	return nextDataState;
}

DATA_STATE flush(Connection * server, uint32_t * expectedSeqNum, uint32_t * highestRecvSeqNum, uint32_t * clientSeqNum) {
	DATA_STATE nextDataState = IN_ORDER;
	uint8_t data[MAX_PAYLOAD_SIZE];
	uint8_t flag;
	int dataLen = 0;
	
	while (*expectedSeqNum <= *highestRecvSeqNum && getDataFromBuffer(*expectedSeqNum, &flag, data, &dataLen) == DATA_VALID) {
		if (flag == END_OF_FILE) return DATA_STATE_DONE;
		#ifdef DEBUG
			printf("Flushing data with seq #%d\n", *expectedSeqNum);
		#endif
		(*expectedSeqNum)++;
		write(outputFileDesc, &data, dataLen);
	}

	if (*expectedSeqNum < *highestRecvSeqNum) {
		// rr expected to open window and srej expected to get missing data
		sendRR(server, clientSeqNum, *expectedSeqNum);
		sendSREJ(server, clientSeqNum, *expectedSeqNum);
		nextDataState = BUFFER;
	} else {
		//rr
		#ifdef DEBUG
			printf("Done flushing, sending RR for seq #%d, going to in-order\n", *expectedSeqNum);
		#endif
		sendRR(server, clientSeqNum, *expectedSeqNum);
		nextDataState = IN_ORDER;
	}

	return nextDataState;
}

void sendRR(Connection * server, uint32_t * clientSeqNum, uint32_t expectedSeqNum) {
	uint32_t expectedSeqNum_n = htonl(expectedSeqNum);
	sendPDU(server, (uint8_t *) &expectedSeqNum_n, sizeof(expectedSeqNum_n), RR, *clientSeqNum);
	(*clientSeqNum)++;
}

void sendSREJ(Connection * server, uint32_t *clientSeqNum, uint32_t missingSeqNum) {
	uint32_t missingSeqNum_n = htonl(missingSeqNum);
	sendPDU(server, (uint8_t *) &missingSeqNum_n, sizeof(missingSeqNum_n), SREJ, *clientSeqNum);
	(*clientSeqNum)++;
}

STATE sendEOFAck(Connection * server, uint32_t * clientSeqNum) {
	sendFlagOnly(server, EOF_ACK, *clientSeqNum);
	(*clientSeqNum)++;
	printf("File done!\n");
	return DONE;
}

void sendFilenamePDU(Parameters * myParameters, int clientSeqNum, Connection * server) {
    uint8_t payload[MAX_PAYLOAD_SIZE];
	int payloadLen = SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE + myParameters->fromFilenameLen + 1;

	// copy in buffer size, window size, and filename (null included)
	uint32_t bufferSize_n = htonl(myParameters->bufferSize);
	memcpy(payload, &bufferSize_n, SIZE_OF_BUFFER_SIZE);
	uint32_t windowSize_n = htonl(myParameters->windowSize);
	memcpy(payload + SIZE_OF_BUFFER_SIZE, &windowSize_n, SIZE_OF_WINDOW_SIZE);
	memcpy(payload + SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE, &myParameters->fromFilename, myParameters->fromFilenameLen + 1); // copy the null in

	sendPDU(server, payload, payloadLen, FNAME, clientSeqNum);
}

void checkArgs(int argc, char * argv[], Parameters * myParameters)
{
	char (*fromFilename)[MAX_FILENAME_LENGTH + 1] = &myParameters->fromFilename;
	int *fromFilenameLen = &myParameters->fromFilenameLen;
	char (*toFilename)[MAX_FILENAME_LENGTH + 1] = &myParameters->toFilename;
	int *toFilenameLen = &myParameters->toFilenameLen;
	int *windowSize = &myParameters->windowSize;
	int *bufferSize = &myParameters->bufferSize;
	double *errorRate = &myParameters->errorRate;
	char (*remoteMachine)[MAX_REMOTE_MACHINE_NAME_LEN] = &myParameters->remoteMachine;
	int *portNumber = &myParameters->portNumber;

        /* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: %s from-filename to-filename window-size buffer-size error-rate remote-machine remote-port \n", argv[0]);
		exit(1);
	}

	*fromFilenameLen = strlen(argv[1]);
	*toFilenameLen = strlen(argv[2]);
	if (*fromFilenameLen > MAX_FILENAME_LENGTH || *toFilenameLen > MAX_FILENAME_LENGTH) {
		printf("from-filename and to-filename must be less than 100 characters\n");
		exit(1);
	}
	memcpy(*fromFilename, argv[1], *fromFilenameLen + 1);
	memcpy(*toFilename, argv[2], *toFilenameLen + 1);

	*windowSize = atoi(argv[3]);
	*bufferSize = atoi(argv[4]);
	if (*bufferSize < 400 || *bufferSize > 1400) {
		printf("buffer-size (given as %d) must be between 400 and 1400\n", *bufferSize);
		exit(1);
	}

	if ((*errorRate = atof(argv[5])) == 0 || *errorRate >= 1) {
		printf("error-rate (given as %s) must be between 0 and 1\n", argv[5]);
		exit(1);
	}
	
	memcpy(remoteMachine, argv[6], MAX_REMOTE_MACHINE_NAME_LEN);
	*portNumber = atoi(argv[7]);

	#ifdef DEBUG
		printf("From Filename: %s\n", *fromFilename);
		printf("From Filename Length: %d\n", *fromFilenameLen);
		printf("To Filename: %s\n", *toFilename);
		printf("To Filename Length: %d\n", *toFilenameLen);
		printf("Window Size: %d\n", *windowSize);
		printf("Buffer Size: %d\n", *bufferSize);
		printf("Error Rate: %.2f\n", *errorRate);
		printf("Remote Machine: %s\n", *remoteMachine);
		printf("Port Number: %d\n", *portNumber);
	#endif
}





