# CSCE 463 – Networks and Distributed Processing  
**Fall 2025**

## HW3: Reliable Transport Service (pseudo TCP)

This project implements a transport layer service over UDP in three stages:

### Part 1: Handshake
- Basic TCP-like handshake connection setup.

### Part 2: Single TCP-like connection 
- Extends Part 1 by implementing many of the TCP features (dynamic RTOs, fast retxs, checksum) for one single packet.

### Part 3: Multi-Threaded 
- Enhances Part 2 with a sender window greater than 1 for improved performance.

> **Note:** This repository is designed to compile and run **only on Windows**.
