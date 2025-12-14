# Selective Reject ARQ File Transfer Protocol

**CPE 464 — Spring 2025**  
Author: **Luke Trusheim**  

---

## Overview

This project is a small, TCP-like file transfer built on top of UDP. UDP by itself can drop, duplicate, or reorder packets, so the code implements its own reliability:

- A sliding window so multiple packets can be “in flight” at once.
- Cumulative acknowledgments (`RR`) to advance the window.
- Targeted retransmissions (`SREJ`) to plug individual gaps without resending everything.
- Checksums to detect bit flips.
- An EOF/ACK handshake so both sides agree the transfer is done.

`rcopy` is the client: it asks for a file, buffers any out-of-order packets, requests resends for missing ones, and writes the result to disk. `server` is the sender: it forks per client, reads the file, keeps a circular send window, and retransmits on SREJ or timeout. Both use `sendtoErr()` to inject controlled loss/corruption so you can observe recovery behavior.

---

## Repository Layout

- `rcopy.c` — client state machine, buffering, RR/SREJ logic, file writeout
- `server.c` — fork-per-client UDP server, windowed sender, EOF/ack teardown
- `slidingWindow.c/h` — sender-side circular window and resend helpers
- `buffer.c/h` — receiver-side circular buffer for out-of-order PDUs
- `pduHelpers.c/h` — PDU construction/parsing, checksum helpers
- `pollLib.c/h`, `networks.c/h`, `safeUtil.c/h`, `gethostbyname.c/h` — provided course utilities
- `sharedConstants.h`, `connection.h`, `checksum.h` — protocol constants and shared types
- `Makefile`, `libcpe464.2.21.a` — build rules and provided `sendtoErr` library

---

## Building

```bash
make          # builds rcopy and server (udpAll target)
make tcpAll   # builds sample TCP programs (not used by SREJ)
make clean    # remove binaries and object files
```

---

## Running

### Server

```bash
./server <error-rate> [port]
```

- `error-rate`: 0 <= p < 1, passed to `sendtoErr_init()`
- `port` optional: if omitted, the OS chooses one and it is printed at startup

Example:

```bash
./server 0.1
Server using Port #: 52054
```

### Client

```bash
./rcopy from-file to-file window-size buffer-size error-rate remote-machine remote-port
```

- `window-size`: receiver sliding window size (also sent to server for sender window)
- `buffer-size`: bytes per data chunk (1–1400), sent to server so it reads that many bytes from disk
- `error-rate`: 0 <= p < 1, for the client’s `sendtoErr_init()`
- `remote-machine`: host running `server`
- `remote-port`: port printed by the server

Example:

```bash
./rcopy fromFile.txt toFile.txt 10 1000 0.05 localhost 52054
```

---

## Protocol Details

- **PDU format:** 4-byte sequence number (network order) + 2-byte checksum + 1-byte flag + payload (0–1400 bytes).
- **Flags (see `sharedConstants.h`):**
  - `FNAME` (8): client request, payload = buffer size (4 bytes) + window size (4 bytes) + null-terminated filename
  - `FNAME_STATUS` (9) with payload `FNAME_OK` (32) or `FNAME_BAD` (33)
  - `FNAME_OK_ACK` (34): client ack after OK
  - `DATA` (16): new data
  - `RESENT_DATA_SREJ` (17) / `RESENT_DATA_TIMEOUT` (18): retransmits
  - `RR` (5) / `SREJ` (6): payload is the expected/missing seqNum (network order)
    - `RR` = Receive Ready. Tells the sender “I have everything up to seq N-1; next in-order is N.” This is a cumulative ACK.
    - `SREJ` = Selective Reject. Tells the sender “I am missing seq N; please resend just that one.” This is a targeted NACK.
  - `END_OF_FILE` (10) / `EOF_ACK` (35)

### Connection Setup
1. `rcopy` sends `FNAME` with requested file name, desired window size, and buffer size.
2. Server replies `FNAME_STATUS` with `FNAME_OK` or `FNAME_BAD`.
3. If OK, `rcopy` sends `FNAME_OK_ACK` and transitions to data reception; on BAD it exits.

### Data Transfer & Reliability
- Server uses the client-provided window size to size a circular send window (`slidingWindow`); it reads `buffer-size` bytes at a time from disk.
- While the window is open, the server sends immediately and polls non-blocking for responses; when closed it polls for 1 s and resends the lowest unacked seqNum on timeout (up to 10 tries via `processPoll`).
- `RR` advances the lower edge of the window; `SREJ` triggers targeted retransmission of that seqNum.
- Receiver (`rcopy`) verifies checksum and drops corrupted PDUs. States: `IN_ORDER` writes immediately and RRs the next expected seqNum; `BUFFER` stores out-of-order data in `buffer.c` and SREJs the gap; `FLUSH` drains buffered contiguous data. `END_OF_FILE` acts as a flag-only PDU to trigger teardown.
- After all data, the server waits for the final RR that frees the window, then sends `END_OF_FILE` and waits for `EOF_ACK` before exiting. The client sends `EOF_ACK` once the file is complete.
- If the client polls for 10 seconds without data during transfer, it assumes the server is gone and terminates.

---

## Notes

- Sockets are IPv6 (`AF_INET6`) but work with IPv4 hosts via IPv4-mapped addresses.
- Sequence numbers start at 0 for both control and data PDUs.
- The provided library `libcpe464.2.21.a` supplies `sendtoErr` and checksum helpers; it is linked by the `Makefile`.
