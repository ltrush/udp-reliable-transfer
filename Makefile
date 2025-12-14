# Makefile for CPE464 UDP/TCP test code
# reorganized for src/include/bin layout

CC       = gcc
SRC_DIR  = src
INC_DIR  = include
LIB_DIR  = lib
BIN_DIR  = bin
BUILD_DIR= build
OBJ_DIR  = $(BUILD_DIR)/obj

CFLAGS = -g -Wall -I$(INC_DIR) -D__LIBCPE464_
LIBS   = $(LIB_DIR)/libcpe464.2.21.a -lstdc++ -ldl

OBJS = \
	$(OBJ_DIR)/networks.o \
	$(OBJ_DIR)/gethostbyname.o \
	$(OBJ_DIR)/pollLib.o \
	$(OBJ_DIR)/safeUtil.o \
	$(OBJ_DIR)/pduHelpers.o \
	$(OBJ_DIR)/slidingWindow.o \
	$(OBJ_DIR)/buffer.o

all: udpAll

udpAll: $(BIN_DIR)/rcopy $(BIN_DIR)/server
tcpAll: $(BIN_DIR)/myClient $(BIN_DIR)/myServer

$(BIN_DIR)/rcopy: $(OBJ_DIR)/rcopy.o $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/server: $(OBJ_DIR)/server.o $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/myClient: $(OBJ_DIR)/myClient.o $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/myServer: $(OBJ_DIR)/myServer.o $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

.PHONY: clean cleano

cleano:
	rm -f $(OBJ_DIR)/*.o

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)


