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

#include "cpe464.h"
#include "connection.h"
#include "sharedConstants.h"
#include "pduHelpers.h"
#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"

#define DEBUG
#define MAXBUF 80

typedef enum State STATE;

enum State {
    START, 
    DONE, 
    FILENAME, 
    SEND_DATA, 
    WAIT_ON_ACK, 
    TIMEOUT_ON_ACK, 
    WAIT_ON_EOF_ACK, 
    TIMEOUT_ON_EOF_ACK
};

STATE filenameState(struct sockaddr_in6 * clientInfo, int clientAddrLen, uint8_t * payload, int pduLen, int32_t * dataFile, uint32_t * bufferSize, uint32_t * windowSize, int * clientSocketNum);
void handleZombies(int sig);
void processServer(int serverSocketNum, double errorRate);
void processClient(uint8_t * pdu, int pduLen, struct sockaddr_in6 * clientInfo, int clientAddrLen);
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
	Connection server = {
		.socketNum = -1,
		.info = {0},
		.addrLen = sizeof(struct sockaddr_in6)
	};
	struct sockaddr_in6 clientInfo;		
	int clientAddrLen = sizeof(clientInfo);

    uint8_t flag = 0;
    uint32_t seq_num = 0;

    // We are going to fork() so need to clean up (SIGCHLD)
    signal(SIGCHLD, handleZombies);

    // get new client connection, fork() child,
    while (1) {
        // block waiting for a new client
		int pduLen = safeRecvfrom(serverSocketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *) &clientInfo, &clientAddrLen);
        if (checkChecksum(pdu, pduLen) == CRC_ERROR)
            continue;

        if ((pid = fork()) < 0) {
            perror("fork");
            exit(-1);
        }

        if (pid == 0) {
            // child process - a new process for each client
            printf("Child fork() - child pid: %d\n", getpid());
			sendErr_init(errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
			close(serverSocketNum);
            processClient(pdu, pduLen, &clientInfo, clientAddrLen);
            exit(0);
        }
    }
}

void processClient(uint8_t * pdu, int pduLen, struct sockaddr_in6 * clientInfo, int clientAddrLen)
{
    STATE state = START;
    int dataFile = 0;
    // int32_t packet_len = 0;
    // uint8_t packet[MAX_LEN];
    uint32_t bufferSize = 0;
	uint32_t windowSize = 0;
	int clientSocketNum = 0;
    uint32_t seq_num = START_SEQ_NUM;

    while (state != DONE)
    {
        switch (state)
        {
            case START:
                state = FILENAME;
                break;

            case FILENAME:
                state = filenameState(clientInfo, clientAddrLen, pdu + PDU_HEADER_SIZE, pduLen, &dataFile, &bufferSize, &windowSize, &clientSocketNum);
                break;

            // case SEND_DATA:
            //     state = send_data(client, packet, &packet_len, data_file, buf_size, &seq_num);
            //     break;

            // case WAIT_ON_ACK:
            //     state = wait_on_ack(client);
            //     break;

            // case TIMEOUT_ON_ACK:
            //     state = timeout_on_ack(client, packet, packet_len);
            //     break;

            // case WAIT_ON_EOF_ACK:
            //     state = wait_on_eof_ack(client);
            //     break;

            // case TIMEOUT_ON_EOF_ACK:
            //     state = timeout_on_eof_ack(client, packet, packet_len);
            //     break;

            case DONE:
                break;

            default:
                printf("In default and you should not be here!!!!\n");
                state = DONE;
                break;
        }
    }
}

STATE filenameState(struct sockaddr_in6 * clientInfo, int clientAddrLen, uint8_t * payload, int pduLen, int32_t * dataFile, uint32_t * bufferSize, uint32_t * windowSize, int * clientSocketNum)
{
    uint8_t response[1];
    char filename[MAX_FILENAME_LENGTH + 1];
    STATE returnValue = DONE;

    // extract buffer sized used for sending data and also filename
	memcpy(bufferSize, payload, SIZE_OF_BUFFER_SIZE);
    *bufferSize = ntohl(*bufferSize);

	memcpy(windowSize, payload + SIZE_OF_BUFFER_SIZE, SIZE_OF_WINDOW_SIZE);
    *windowSize = ntohl(*windowSize);

    memcpy(filename, payload + SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE, pduLen - SIZE_OF_BUFFER_SIZE - SIZE_OF_WINDOW_SIZE);

	#ifdef DEBUG
		printf("Received buffer size of %u\n", *bufferSize);
		printf("Received window size of %u\n", *windowSize);
		printf("Received filename \"%s\"\n", filename);
	#endif

    /* Create client socket to allow for processing this particular client */
    *clientSocketNum = safeGetUdpSocket();

	Connection client = {
		.socketNum = *clientSocketNum,
		.info = *clientInfo,
		.addrLen = clientAddrLen
	};

	#ifdef DEBUG
		printf("Will be using socket %d to communicate with rcopy\n", *clientSocketNum);
	#endif

    if (((*dataFile) = open(filename, O_RDONLY)) < 0)
    {
		#ifdef DEBUG
			printf("Couldn't find file with that name\n");
		#endif
		
		// sendFlagOnly(*clientSocketNum, (struct sockaddr *) clientInfo, clientAddrLen, FNAME_BAD);
		sendFlagOnly(&client, FNAME_BAD);
        returnValue = DONE;
    }
    else
    {
		#ifdef DEBUG
			printf("Found file with that name\n");
		#endif

		// sendFlagOnly(*clientSocketNum, (struct sockaddr *) clientInfo, clientAddrLen, FNAME_OK);
        sendFlagOnly(&client, FNAME_OK);
        returnValue = SEND_DATA;
    }

    return returnValue;
}

void oldprocessClient(int serverSocketNum)
{
	int dataLen = 0; 
	char buffer[MAX_PDU_SIZE];	  
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = safeRecvfrom(serverSocketNum, buffer, MAX_PDU_SIZE, 0, (struct sockaddr *) &client, &clientAddrLen);
		printf("Received the following PDU:\n");
		printPDU(buffer, dataLen);
		printf("Received message from client with ");
		printIPInfo(&client);
		// printf(" Len: %d \'%s\'\n", dataLen, buffer + PDU_HEADER_SIZE);


		// just for fun send back to client number of bytes received
		// sprintf(buffer, "bytes: %d", dataLen);
		// safeSendto(serverSocketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);
		safeSendto(serverSocketNum, buffer, dataLen, 0, (struct sockaddr *) & client, clientAddrLen);
	}
}

void checkArgs(int argc, char * argv[], double * errorRate, int * portNumber)
{
        /* check command line arguments  */
	if (argc != 3 && argc != 2)
	{
		fprintf(stderr, "Usage %s error-rate [optional port number]\n", argv[0]);
		exit(1);
	}

	if ((*errorRate = atof(argv[1])) == 0 || *errorRate >= 1) {
		printf("error-rate must be between 0 and 1\n");
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