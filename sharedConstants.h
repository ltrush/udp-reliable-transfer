#define MAX_PAYLOAD_SIZE 1400
#define MAX_FILENAME_LENGTH 100 // does not include null
#define PDU_HEADER_SIZE 7
#define MAX_PDU_SIZE (MAX_PAYLOAD_SIZE + PDU_HEADER_SIZE)
#define SIZE_OF_BUFFER_SIZE 4
#define SIZE_OF_WINDOW_SIZE 4
#define START_SEQ_NUM 0

enum FLAG {
    FNAME = 7,
    DATA = 3,
    FNAME_OK = 8,
    FNAME_BAD = 9,
    ACK = 5,
    END_OF_FILE = 10,
    EOF_ACK = 11,
    CRC_ERROR = -1 // should this really be in flag?
};