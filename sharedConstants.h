#ifndef SHAREDCONSTANTS_H
#define SHAREDCONSTANTS_H

#define MAX_PAYLOAD_SIZE 1400
#define MAX_FILENAME_LENGTH 100 // does not include null
#define PDU_HEADER_SIZE 7
#define MAX_PDU_SIZE (MAX_PAYLOAD_SIZE + PDU_HEADER_SIZE)
#define SIZE_OF_BUFFER_SIZE 4
#define SIZE_OF_WINDOW_SIZE 4

#define START_SEQ_NUM 0
#define SETUP_SEQ_NUM 0

#define MAX_TRIES 10

enum FLAG {
    RR = 5,
    SREJ = 6,
    FNAME = 8,
    FNAME_STATUS = 9,
    END_OF_FILE = 10,
    DATA = 16,
    RESENT_DATA_SREJ = 17,
    RESENT_DATA_TIMEOUT = 18,
    FNAME_OK = 32,
    FNAME_BAD = 33,
    FNAME_OK_ACK = 34,
    EOF_ACK = 35,
    CRC_ERROR = -1 // should this really be in flag?
};

#endif