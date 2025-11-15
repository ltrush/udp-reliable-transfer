# Program 3 — Selective Reject ARQ

**CPE 464 — Spring 2025**
Author: **Luke Trusheim**
Lab Section: **(insert lab time)**

---

## Overview

This project implements a **Selective Reject (SREJ) sliding-window file transfer protocol** over **UDP**, following the full specification of Programming Assignment #3 for CPE 464. 

The goal of this project is to reliably transfer a file from a server to a client despite packet loss, corruption, duplication, delay, and reordering—all while using a custom application-level protocol layered on top of unreliable datagrams.

This repository contains two programs:

* **rcopy** — the client that requests a file from the server, receives packets, handles buffering/SREJ/RR logic, and writes the final output file.
* **server** — the multiprocess UDP server that listens for incoming rcopy requests, forks child processes to handle each client concurrently, and performs sliding-window transmission of file data.

Both programs use a **custom PDU format**, **internet checksum**, **circular buffering**, and **sendtoErr()** for every single packet transmission.

---

## Program Structure

(Adjust names if yours differ.)

```
.
├── rcopy.c
├── server.c
├── window.c           <-- circular window/buffer library
├── window.h
├── pdu.c              <-- packet encoding/decoding helpers
├── pdu.h
├── checksum.c         <-- Internet checksum (provided template)
├── checksum.h
├── pollLib.c          <-- provided poll library
├── pollLib.h
├── Makefile
└── README.md
```

---

## Building

```
make
```

Produces the binaries:

* `rcopy`
* `server`

---

## Running the Programs

### Server

```
server <error-rate> [optional-port-number]
```

Example:

```
server .1
Server is using port 1234
```

### Client (rcopy)

```
rcopy from-file to-file window-size buffer-size error-rate remote-machine remote-port
```

Example:

```
rcopy input.txt output.txt 10 1000 .1 unix1.csc.calpoly.edu 1234
```

---

## Protocol Summary

### Header (7 bytes)

* **4 bytes** sequence number (network order)
* **2 bytes** checksum
* **1 byte** flag

### Data Payload

* 1–1400 bytes of file data
* OR a 4-byte sequence number (RR / SREJ)
* OR filename + parameters during setup

### Flags (per assignment requirements)

```
5   RR
6   SREJ
8   Request (filename + window + buffer)
9   Server response to request
10  EOF / final data packet
16  Regular data packet
17  Resent due to SREJ
18  Resent due to timeout
>=32 User-defined flags (if needed)
```

### Sender Logic (server)

* Maintains circular window buffer
* Sends while window is open (non-blocking poll())
* When window closes, uses 1-second blocking poll()
* On timeout in closed-window state, resends lowest unacknowledged packet
* Respects strict limits on when timeouts are allowed

### Receiver Logic (rcopy)

* Verifies checksum; drops corrupted packets
* Buffers out-of-order data in circular buffer
* Sends RR for contiguous progress
* Sends SREJ for missing packets
* Writes in-order data to disk
* Manages clean teardown

---

## Acknowledgments

This project was completed for **CPE 464 – Spring 2025** at **Cal Poly, San Luis Obispo**.

The following materials were **provided by the course staff** and are acknowledged accordingly:

* **poll library** (pollLib.c / pollLib.h)
* **sendtoErr()** and **sendtoErr_init()**
* **Internet checksum implementation**
* Any instructor-provided header/skeleton templates
* The assignment document defining the protocol requirements 

All other logic—including the sliding-window design, buffering system, state machines, retransmission logic, packet handling, forked server architecture, teardown procedure, error handling, and all additional C source code—was written entirely by **Luke Trusheim**.

---
Here are tighter, clearer versions of the **Skills Used** and **What I Learned** sections—lean, direct, and still professional.

If you want them even shorter (bullet-only), tell me.

---

## Skills Used

* **UDP Socket Programming:** Building a custom transport protocol over datagrams, managing addresses, headers, checksums, and byte order.
* **Sliding Window + SREJ:** Implementing window management, retransmissions, RR/SREJ logic, and out-of-order handling.
* **Circular Buffer Design:** Creating a malloc’d, index-based ring buffer for both sender and receiver without external data structures.
* **Event-Driven I/O:** Using poll() correctly for both non-blocking and 1-second blocking states depending on window openness.
* **Multiprocessing:** Using fork() to serve multiple clients and handling correct re-initialization of sendtoErr() per child.
* **Robust Systems-Level C:** Careful pointer use, buffer management, error handling, and strict adherence to assignment constraints.

---

## What I Learned

* How unreliable network behavior (loss, corruption, duplication, delay) forces careful protocol design.
* How Selective Reject ARQ works in practice and why precise window logic matters.
* How to manage timing without sleep() and rely entirely on poll()-based event loops.
* How to structure sender/receiver state machines that handle many edge cases cleanly.
* How concurrency (forked server processes) interacts with networking and error injection.
* The amount of hidden complexity that real protocols like TCP abstract away.
