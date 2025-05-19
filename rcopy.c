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
	DONE, FILENAME, RECV_DATA, FILE_OK, START_STATE
};

STATE startState(Parameters * myParameters, struct sockaddr_in6 * server, int * socketNum);
STATE filenameState(char * fromFilename, struct sockaddr_in6 server, int socketNum);
STATE fileOkState(int * outputFileDesc, char * toFilename);

void processFile(Parameters * myParameters);
void talkToServer(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
void checkArgs(int argc, char * argv[], Parameters * myParameters);

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	Parameters myParameters = {0};
	
	checkArgs(argc, argv, &myParameters);

	sendErr_init(myParameters.errorRate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

	// socketNum = setupUdpClientToServer(&server, myParameters.remoteMachine, myParameters.portNumber);

	processFile(&myParameters);
	
	// talkToServer(socketNum, &server);
	
	close(socketNum);

	return 0;
}

void processFile(Parameters * myParameters) {
	int32_t outputFileDesc = 0;
	int socketNum = 0;
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	uint32_t clientSeqNum = 0;
	STATE state = START_STATE;

	while (state != DONE) {
		switch (state)
		{
			case START_STATE:
				state = startState(myParameters, &server, &socketNum);

			case FILENAME:
				state = filenameState(myParameters->fromFilename, server, socketNum);
				break;

			case FILE_OK:
				// state = fileOkState(setup_output_file_fd(argv[2]), server, clientseqnum);
				break;

			case RECV_DATA:
				// state = recv_data(out_file_fd, server, clientseqnum);
				break;

			case DONE:
				break;

			default:
				printf("ERROR - in default state\n");
				break;
		}
	}
}
//change this from pointer to actual thing (just sockaddr_in)
STATE startState(Parameters * myParameters, struct sockaddr_in6 * server, int * socketNum)
{
    // Returns FILENAME if no error; otherwise DONE (too many connects, cannot connect to server)
    uint8_t pdu[MAX_PDU_SIZE];
    uint8_t payload[MAX_PAYLOAD_SIZE];
	uint32_t sequenceNum = 0;
    STATE returnValue = FILENAME;

    // if we have connected to server before, close it before reconnect
    if (*socketNum > 0)
    {
        close(*socketNum);
    }

	*socketNum = setupUdpClientToServer(server, myParameters->remoteMachine, myParameters->portNumber);
	#ifdef DEBUG
		printf("Will be using socket %d to communicate with server\n", *socketNum);
	#endif

    // if (setupUdpClientToServer(argv[5], atoi(argv[6]), server) < 0)
    // {
    //     // could not connect to server
    //     returnValue = DONE;
    // }
    // else
    // {
        // put in buffer size (for sending data) and filename
        uint32_t bufferSize_n = htonl(myParameters->bufferSize);
        memcpy(payload, &bufferSize_n, SIZE_OF_BUFFER_SIZE);
		uint32_t windowSize_n = htonl(myParameters->windowSize);
        memcpy(payload + SIZE_OF_BUFFER_SIZE, &windowSize_n, SIZE_OF_WINDOW_SIZE);
		memcpy(payload + SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE, &myParameters->fromFilename, myParameters->fromFilenameLen + 1);
        // printIPv6Info(&server->remote);
		int pduLen = createPDU(pdu, sequenceNum, FNAME, payload, SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE + myParameters->fromFilenameLen + 1);
		int serverAddrLen = sizeof(struct sockaddr_in6);
		safeSendto(*socketNum, pdu, pduLen, 0, (struct sockaddr *) server, serverAddrLen);
		
        // send_buf(buf, fileNameLen + SIZE_OF_BUF_SIZE, server, FNAME, *clientSeqNum, packet);

        returnValue = FILENAME;
    // }

    return returnValue;
}

STATE filenameState(char * fromFilename, struct sockaddr_in6 server, int socketNum) {
	// Send the file name, get response
	// return START_STATE if no reply from server, DONE if bad filename, FILE_OK otherwise
	int nextState = DONE;
	uint8_t pdu[MAX_PDU_SIZE];
	uint8_t flag = 0;
	uint32_t seqNum = 0;
	static int retryCount = 0;

	// if ((returnValue = processPoll(server, &retryCount, START_STATE, FILE_OK, DONE)) == FILE_OK)
	{
		// recv_check = recv_buf(packet, MAX_LEN, server->sk_num, server, &flag, &seq_num);
		int serverAddrLen = sizeof(struct sockaddr_in6);
		int pduLen = safeRecvfrom(socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *) &server, &serverAddrLen);
		retrieveHeader(pdu, pduLen, &flag, &seqNum);
		// check for bit flip
		if (checkChecksum(pdu, pduLen) == CRC_ERROR)
		{
			#ifdef DEBUG
				printf("Filename status PDU corrupted\n");
			#endif
			nextState = START_STATE;
		}
		else if (flag == FNAME_BAD)
		{
			printf("File \"%s\" not found\n", fromFilename);
			nextState = DONE;
		}
		else if (flag == FNAME_OK) // IF WE JUST GET STRAIGHT DATA AND NO FILENAME STATUS, WE ASSUME ITS GOOD AND START TO RECV DATA?
		{
			// file yes/no packet lost - instead its a data packet
			#ifdef DEBUG
				printf("File \"%s\" found\n", fromFilename);
			#endif
			nextState = FILE_OK;
			
		}
		// else if (flag == DATA) // IF WE JUST GET STRAIGHT DATA AND NO FILENAME STATUS, WE ASSUME ITS GOOD AND START TO RECV DATA?
		// {
		// 	// file yes/no packet lost - instead its a data packet
		// 	#ifdef DEBUG
		// 		printf("File \"%s\" found\n", fromFilename);
		// 	#endif
		// 	nextState = FILE_OK;
			
		// }
	}

	return(nextState);
}

STATE fileOkState(int * outputFileDesc, char * toFilename)
{
    STATE returnValue = DONE;

    if ((*outputFileDesc = open(toFilename, O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0)
    {
        perror("File open error: ");
        returnValue = DONE;
    }
    else
    {
		// int pduLen = createPDU(pdu, sequenceNum, FNAME, payload, SIZE_OF_BUFFER_SIZE + SIZE_OF_WINDOW_SIZE + myParameters->fromFilenameLen + 1);
        returnValue = RECV_DATA;
    }

    return returnValue;
}

// int processPoll(Connection * client, int * retryCount,
//                   int selectTimeoutState, int dataReadyState, int doneState)
// {
//     // Returns:
//     // doneState if calling this function exceeds MAX_TRIES
//     // selectTimeoutState if the select times out without receiving anything
//     // dataReadyState if select() returns indicating that data is ready for read

//     int returnValue = dataReadyState;

//     (*retryCount)++;
//     if (*retryCount > MAX_TRIES)
//     {
//         printf("No response for other side for %d seconds, terminating connection\n", MAX_TRIES);
//         returnValue = doneState;
//     }
//     else if (pollCall(client->sk_num, SHORT_TIME, 0) == 1)
//     {
//         *retryCount = 0;
//         returnValue = dataReadyState;
//     }
//     else
//     {
//         // no data ready
//         returnValue = selectTimeoutState;
//     }

//     return returnValue;
// }



// STATE recv_data(int32_t output_file, Connection * server, uint32_t * clientSeqNum)
// {
//     uint32_t seq_num = 0;
//     uint32_t ackSeqNum = 0;
//     uint8_t flag = 0;
//     int32_t data_len = 0;
//     uint8_t data_buf[MAX_LEN];
//     uint8_t packet[MAX_LEN];
//     static int32_t expected_seq_num = START_SEQ_NUM;

//     if (select_call(server->sk_num, LONG_TIME, 0) == 0)
//     {
//         printf("Timeout after 10 seconds, server must be gone.\n");
//         return DONE;
//     }

//     data_len = recv_buf(data_buf, MAX_LEN, server->sk_num, server, &flag, &seq_num);

//     /* do state RECV_DATA again if there is a crc error (don't send ack, don't write data) */
//     if (data_len == CRC_ERROR)
//     {
//         return RECV_DATA;
//     }

//     if (flag == END_OF_FILE)
//     {
//         // send ACK
//         send_buf(packet, 1, server, EOF_ACK, *clientSeqNum, packet);
//         (*clientSeqNum)++;
//         printf("File done!\n");
//         return DONE;
//     }
//     else
//     {
//         // send ACK
//         ackSeqNum = htonl(seq_num);
//         send_buf((uint8_t *) &ackSeqNum, sizeof(ackSeqNum), server, ACK, *clientSeqNum, packet);
//         (*clientSeqNum)++;
//     }

//     if (seq_num == expected_seq_num)
//     {
//         expected_seq_num++;
//         write(output_file, &data_buf, data_len);
//     }

//     return RECV_DATA;
// }




void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	uint32_t sequenceNum = 0;
	uint8_t flag = 234;
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0; 
	uint8_t buffer[MAX_PAYLOAD_SIZE] = {0};
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin((char *)buffer);

		uint8_t pdu[MAX_PDU_SIZE] = {0};
		int pduLen = createPDU(pdu, sequenceNum++, flag, buffer, dataLen);
		printf("Sending the following PDU:\n");
		printPDU(pdu, pduLen);
	
		safeSendto(socketNum, pdu, pduLen, 0, (struct sockaddr *) server, serverAddrLen);

		int dataLen = safeRecvfrom(socketNum, pdu, MAX_PDU_SIZE, 0, (struct sockaddr *) server, &serverAddrLen);
		printf("Received the following PDU:\n");
		printPDU(pdu, dataLen);
		// printf("Received message from server with ");
		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	      
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAX_PAYLOAD_SIZE) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
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





