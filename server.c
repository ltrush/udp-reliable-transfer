/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>

#include "cpe464.h"
#include "pollLib.h"
#include "slidingWindow.h"
#include "connection.h"
#include "sharedConstants.h"
#include "pduHelpers.h"
#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"

// #define DEBUG
#define MAXBUF 80

typedef enum State STATE;

enum State {
    START, 
    DONE, 
    PROCESS_FILENAME, 
	WAIT_ON_FILENAME_OK_ACK,
    SEND_DATA, 
	SEND_EOF,
    RECV_POSSIBLE_EOF_ACK
};

STATE processFilename(Connection * client, uint8_t * payload, int pduLen, int32_t * dataFile, uint32_t * bufferSize, uint32_t * windowSize);
STATE filenameOk(Connection * client);
STATE sendData(Connection * client, int dataFile, int windowSize, int bufferSize, uint32_t *seqNum);
STATE sendEofAndWaitForAck(Connection * client, uint32_t * seqNum);

void handleZombies(int sig);
void processServer(int serverSocketNum, double errorRate);
void processClient(uint8_t * pdu, int pduLen, Connection * client);
void processResponse(Connection * client, bool * waitingForLastRR, uint32_t lastDataSeqNum);
void checkArgs(int argc, char * argv[], double * errorRate, int * portNumber);

int main (int argc, char *argv[])
{ 
	int serverSocketNum = 0;				
	int portNumber = 0;
	double errorRate = 0;

	checkArgs(argc, argv, &errorRate, &portNumber);
		
	serverSocketNum = udpServerSetup(portNumber);

	processServer(serverSocketNum, errorRate);

	// processClient(serverSocketNum);

	close(serverSocketNum);
	
	return 0;
}

void processServer(int serverSocketNum, double errorRate)
{
    pid_t pid = 0;
    uint8_t pdu[MAX_PDU_SIZE];
	Connection client = {
		.socketNum = 0,
		.info = {0},
		.addrLen = sizeof(struct sockaddr_in6)
	};

    // uint8_t flag = 0;
    // uint32_t seq_num = 0;

    signal(SIGCHLD, handleZombies);

    while (1) {
        // block waiting for a new client
		int pduLen = safeRecvfrom(serverSocketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *) &client.info, &client.addrLen);
        if (checkChecksum(pdu, pduLen) == CRC_ERROR)
            continue;

        if ((pid = fork()) < 0) {
            perror("fork");
            exit(-1);
        }

        if (pid == 0) {
			#ifdef DEBUG
            	printf("Forking child with pid: %d\n", getpid());
			#endif
			setupPollSet();
			sendErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);
			close(serverSocketNum);
            processClient(pdu, pduLen, &client);
			#ifdef DEBUG
            	printf("Exiting child with pid: %d\n", getpid());
			#endif
            exit(0);
        }
    }
}

void processClient(uint8_t * pdu, int pduLen, Connection * client)
{
    STATE state = START;
    int dataFile = 0;
    // int32_t packet_len = 0;
    // uint8_t packet[MAX_LEN];
    uint32_t bufferSize = 0;
	uint32_t windowSize = 0;
    uint32_t serverSeqNum = START_SEQ_NUM;

    while (state != DONE)
    {
        switch (state)
        {
            case START:
                state = PROCESS_FILENAME;
                break;

            case PROCESS_FILENAME:
				#ifdef DEBUG
					printf("\nIn PROCESS_FILENAME state\n");
				#endif
                state = processFilename(client, pdu + PDU_HEADER_SIZE, pduLen, &dataFile, &bufferSize, &windowSize);
                break;

			case WAIT_ON_FILENAME_OK_ACK:
				#ifdef DEBUG
					printf("\nIn WAIT_ON_FILENAME_OK_ACK state\n");
				#endif
				state = filenameOk(client);
				break;

            case SEND_DATA:
				#ifdef DEBUG
					printf("\nIn SEND_DATA state\n");
				#endif
                state = sendData(client, dataFile, windowSize, bufferSize, &serverSeqNum);
                break;

            case SEND_EOF:
				#ifdef DEBUG
					printf("\nIn SEND_EOF state\n");
				#endif
                state = sendEofAndWaitForAck(client, &serverSeqNum);
                break;

            case DONE:
				#ifdef DEBUG
					printf("\nIn DONE state\n");
				#endif
                break;

            default:
                printf("In default and you should not be here!!!!\n");
                state = DONE;
                break;
        }
    }
}

STATE processFilename(Connection * client, uint8_t * payload, int pduLen, int32_t * dataFile, uint32_t * bufferSize, uint32_t * windowSize)
{
    char filename[MAX_FILENAME_LENGTH + 1];
    STATE nextState = DONE;

	memcpy(bufferSize, payload, SIZE_OF_BUFFER_SIZE);
    *bufferSize = ntohl(*bufferSize);

	memcpy(windowSize, payload + SIZE_OF_BUFFER_SIZE, SIZE_OF_WINDOW_SIZE);
    *windowSize = ntohl(*windowSize);
	setupWindow(*windowSize);

    memcpy(filename, payload + SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE, pduLen - SIZE_OF_BUFFER_SIZE - SIZE_OF_WINDOW_SIZE);

	#ifdef DEBUG
		printf("Received buffer size of %u\n", *bufferSize);
		printf("Received window size of %u\n", *windowSize);
		printf("Received filename \"%s\"\n", filename);
	#endif

    /* Create client socket to allow for processing this particular client */
	client->socketNum = safeGetUdpSocket();
	addToPollSet(client->socketNum);

	#ifdef DEBUG
		printf("Will be using socket %d to communicate with rcopy\n", client->socketNum);
	#endif

	uint8_t filenameStatus[1] = {0};
    if (((*dataFile) = open(filename, O_RDONLY)) < 0)
    {
		#ifdef DEBUG
			printf("Couldn't find file with that name. Sending FNAME_BAD flag.\n");
		#endif
		
		filenameStatus[0] = FNAME_BAD;
		sendPDU(client, filenameStatus, sizeof(uint8_t), FNAME_STATUS, SETUP_SEQ_NUM);
        nextState = DONE;
    }
    else
    {
		#ifdef DEBUG
			printf("Found file with that name. Sending FNAME_OK flag.\n");
		#endif

		filenameStatus[0] = FNAME_OK;
		sendPDU(client, filenameStatus, sizeof(uint8_t), FNAME_STATUS, SETUP_SEQ_NUM);
        nextState = WAIT_ON_FILENAME_OK_ACK;
    }

    return nextState;
}

// do we have to check if fileename ok ack is corrupted or even check the flag? as long as we got osmehing we know it was an ack
STATE filenameOk(Connection * client) {
	if (pollCall(LONG_TIME) == -1) {
		#ifdef DEBUG
			printf("Timed out waiting for filename-ok ACK\n");
		#endif
		return DONE;
	}

	uint8_t flag;
	uint32_t clientSeqNum;
	int checksumResult;
	uint8_t payload[MAX_PAYLOAD_SIZE];
	retrievePDU(client, &flag, &clientSeqNum, &checksumResult, payload); // don't need to check any of this, receiving packet now implies rcopy ready to receive data
	return SEND_DATA;
}

STATE sendData(Connection * client, int dataFile, int windowSize, int bufferSize, uint32_t *serverSeqNum) {
	// setup stuff. this is okay because we are never returning back to this state
	enum POLL_STATUS {RESPONSE_RECV, CONTINUE, RESEND_LOWEST, QUIT};
	STATE nextState = SEND_DATA;

	int lenRead = 0;	
	int tryCount = 0;
	static int atEOF = 0; // flag for when done reading
	static bool waitingForLastRR = false; // flag for when waiting for last RR
	uint32_t lastDataSeqNum;
    uint8_t payload[MAX_PAYLOAD_SIZE] = {0};

	while (isWindowOpen() && !atEOF) {
		memset(payload, 0, sizeof(payload));
		lenRead = read(dataFile, payload, bufferSize) ;
		if (lenRead == 0) {
			atEOF = 1;
			lastDataSeqNum = (*serverSeqNum) - 1;
		} else if (lenRead == -1) {
			perror("Read error");
			return DONE;
		}
		
		#ifdef DEBUG
			printf("Window is open\n");
			// printf("Sending \"%s\"\n", payload);
		#endif

		if (!atEOF) {
			addDataToWindow(*serverSeqNum, payload, lenRead);
			sendPDU(client, payload, lenRead, DATA, *serverSeqNum);
			(*serverSeqNum)++;
		}
		
		if (pollCall(0) != -1) {
			processResponse(client, &waitingForLastRR, lastDataSeqNum);
		}
	}

	if (atEOF && (lastDataSeqNum + 1) != getLowestSeqNum()) {
		#ifdef DEBUG
			printf("Highest RR received: %d\n", getLowestSeqNum());
			printf("Read EOF, last sent seqNum #%d, waiting on RR %d before sending EOF\n", lastDataSeqNum, lastDataSeqNum + 1);
		#endif
		waitingForLastRR = true;
	}
	
	tryCount = 0;
	while (isWindowClosed() || waitingForLastRR) {
		#ifdef DEBUG
			if (isWindowClosed()) printf("Window is closed\n");
			printf("Waiting for response (try #%d)\n", tryCount + 1);
		#endif

		int pollStatus = processPoll(client, &tryCount, SHORT_TIME, 10, RESEND_LOWEST, RESPONSE_RECV, QUIT);

		switch (pollStatus) {
			case RESEND_LOWEST: {
				uint8_t data[MAX_PAYLOAD_SIZE] = {0};
				uint32_t seqNum = 0;
				int dataLen = getLowestSeqNumData(data, &seqNum);
				#ifdef DEBUG
					printf("Resending lowest data in window\n");
				#endif
				sendPDU(client, data, dataLen, RESENT_DATA_TIMEOUT, seqNum);
				break;
			}
				
			case RESPONSE_RECV: {
				processResponse(client, &waitingForLastRR, lastDataSeqNum);
				break;
			}
			case QUIT:
				return DONE;
				break;

			default:
				break;
		}
	}

	if (!waitingForLastRR && atEOF) {
		#ifdef DEBUG
			printf("Received last RR\n");
		#endif
		nextState = SEND_EOF;
	}

    return nextState;
}

void processResponse(Connection * client, bool * waitingForLastRR, uint32_t lastDataSeqNum) {
	uint8_t flag;
	uint32_t clientSeqNum;
	int checksumResult;
	uint8_t payload[MAX_PAYLOAD_SIZE] = {0};
	retrievePDU(client, &flag, &clientSeqNum, &checksumResult, payload);
	
	if (checksumResult == CRC_ERROR) return;

	#ifdef DEBUG
		switch (flag) {
			case SREJ:
				printf("Received SREJ\n");
				break;

			case RR:
				printf("Received RR\n");
				break;

			case EOF_ACK:
				printf("Received EOF_ACK. Exiting\n");
				break;
			
			default:
				printf("ERROR: In RECV_DATA, unidentified flag from client\n");
				break;
		}
	#endif

	uint32_t targetSeqNum;
	if (flag == SREJ || flag == RR) {
		memcpy(&targetSeqNum, payload, sizeof(uint32_t));
		targetSeqNum = ntohl(targetSeqNum);
	}

	uint8_t data[MAX_PAYLOAD_SIZE] = {0};
	int dataLen = 0;
	switch (flag) {
		case SREJ:
			#ifdef DEBUG
				printf("Sending data with seq #%d\n", targetSeqNum);
			#endif
			dataLen = getDataFromWindow(targetSeqNum, data);
			sendPDU(client, data, dataLen, RESENT_DATA_SREJ, targetSeqNum);
			break;

		case RR:
			#ifdef DEBUG
				printf("Updating lower to be %d\n", targetSeqNum);
			#endif
			updateLower(targetSeqNum);
			if (waitingForLastRR && (targetSeqNum - 1) == lastDataSeqNum) *waitingForLastRR = false;
			break;

		case EOF_ACK:
			close(client->socketNum);
			exit(0);
			break;
		
		default:
			break;
	}
}

// FIGURE OUT/CLEAN UP SEQUENCE NUMBER PASSING AND STUFF
// OTHER ENUMS ARE GONNA FUCK IT UP YOOU CANT DO MULTIPLE ENUMS BC THEY COULD BE THE SAME
STATE sendEofAndWaitForAck(Connection * client, uint32_t * serverSeqNum) {
	STATE nextState = DONE;
	bool waitingForLastRR = false;
	static int tryCount = 0;
	sendFlagOnly(client, END_OF_FILE, *serverSeqNum);
    int pollResult = processPoll(client, &tryCount, SHORT_TIME, 10, SEND_EOF, RECV_POSSIBLE_EOF_ACK, DONE);

	switch (pollResult) {
		case SEND_EOF:
			nextState = SEND_EOF;
			break;
		case RECV_POSSIBLE_EOF_ACK:
			processResponse(client, &waitingForLastRR, *serverSeqNum); // doesn't matter what we put in last field
			nextState = SEND_EOF;
			break;
		case DONE:
			nextState = DONE;
			break;
	}

	return nextState;
}

void checkArgs(int argc, char * argv[], double * errorRate, int * portNumber)
{
        /* check command line arguments  */
	if (argc != 3 && argc != 2)
	{
		fprintf(stderr, "Usage %s error-rate [optional port number]\n", argv[0]);
		exit(1);
	}

	if ((*errorRate = atof(argv[1])) >= 1) {
		printf("error-rate must be >= 0 and < 1\n");
		exit(1);
	}

	if (argc == 3)
	{
		*portNumber = atoi(argv[2]);
	}
}

void handleZombies(int sig) {
	int stat = 0;
	while (waitpid(-1, &stat, WNOHANG) > 0){}
}