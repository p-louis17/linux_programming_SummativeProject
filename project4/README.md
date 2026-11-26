# Project 4: Multithreaded Library Book Reservation System

A C program that simulates a university library using POSIX threads. A student thread submits book reservation requests, a library staff thread approves or rejects them, and a monitoring thread reports statistics. The three threads share one bounded queue and coordinate with a mutex and condition variables.

## Files

* `reservation.c`: the complete source code, with a documentation block at the top
* `output.txt`: sample output from a full run
* `README.md`: this file

## Requirements

* Linux (developed and tested on Ubuntu)
* GCC with POSIX threads support

## Compilation

From the project folder:

```
gcc -Wall -Wextra -pthread -o reservation reservation.c
```

The `-pthread` flag is required to link the threads library. The program compiles without warnings.

## Execution

```
./reservation
```

To also save the output to a file:

```
./reservation | tee output.txt
```

The program takes no command line arguments and reads no input files or user input. All behaviour is controlled by constants at the top of `reservation.c`.

## Inputs (fixed parameters)

| Parameter | Value |
|---|---|
| Queue capacity | 5 requests |
| Total reservations | 15 |
| Student submit time | 1 second per request |
| Staff processing time | 3 seconds per request |
| Monitor interval | 4 seconds |

Reservation IDs are generated in order from 1 to 15. Book IDs are random numbers from 1 to 20. Books with even IDs are available and their reservations are approved. Books with odd IDs are unavailable and their reservations are rejected.

## How it works

1. **Student thread (producer):** every second it creates a request and inserts it into the circular buffer. If the queue is full it prints a message and waits until a slot is free.
2. **Library staff thread (consumer):** it removes one request at a time, takes 3 seconds to process it, then prints APPROVED or REJECTED. If the queue is empty it prints a message and waits.
3. **Monitor thread:** every 4 seconds it prints the total submitted, total processed, approved, rejected and the current queue size.

All shared data is protected by one mutex. Condition variables wake the producer, consumer and monitor when the queue state changes. The slow work (the 1 and 3 second delays) happens outside the lock.

## Expected output

Each line starts with a tag showing which thread printed it:

```
[Student] Submitted reservation #1 for book 9 (queue 1/5)
[Staff]   Queue EMPTY. Waiting for reservations...
[Staff]   Processing reservation #1 for book 9...
[Student] Queue FULL (5/5). Reservation #9 (book 20) is waiting...
[Staff]   Reservation #1 REJECTED (book 9 unavailable)
[Staff]   Reservation #3 APPROVED (book 20 available)
```

Monitor reports look like this:

```
========== MONITOR REPORT ==========
  Total submitted : <number>
  Total processed : <number>
  Approved        : <number>
  Rejected        : <number>
  Queue size      : <number>/5
=====================================
```

A full run shows these events in order:

1. The staff thread waits on an empty queue at the start, because the first request takes 1 second to arrive.
2. The queue fills up, because requests arrive every second but are processed every 3 seconds. The student thread prints a queue full message and waits.
3. The student thread resumes whenever staff remove a request.
4. Monitor reports appear roughly every 4 seconds.
5. After the last reservation the program prints a final report and the closing line:

```
========== FINAL REPORT ==========
  Total submitted : 15
  Total processed : 15
  Approved        : 7
  Rejected        : 8
  Queue size      : 0/5
=====================================

All reservations processed. Program finished.
```

The approved and rejected counts depend on the random book IDs, so they differ between runs. Approved plus rejected always equals 15, and the final queue size is always 0.

## Runtime

A run takes about 46 seconds (1 second for the first submission plus 3 seconds for each of the 15 reservations). The program ends by itself with exit code 0. Press Ctrl+C only if you want to stop it early.

## Termination

After the 15th reservation is processed, the staff thread sets a finished flag and wakes the monitor. The monitor prints the final report and exits. The main thread joins all three threads, then destroys the mutex and condition variables before exiting.
