# Project 1: Investigating and Reverse Engineering an ELF Executable

## Description

program.c is a small C program that simulates a bank transaction processing
system. It stores five transactions in heap memory, processes them in a loop,
separates deposits from withdrawals, and prints the totals and the final
balance. The compiled executable is then analysed with readelf, objdump,
strace and gdb. The analysis is in the project report.

## Files

* program.c        C source code
* program          Stripped ELF executable (no symbols, used for Parts B and C)
* program_debug    Executable built with debugging information (used for Part D)
* README.md        This file
* Report           Analysis report covering Parts B, C and D

## Requirements

* Linux x86-64 (developed and tested on Ubuntu 24.04)
* gcc
* binutils (readelf, objdump, strip)
* strace
* gdb

Install on Ubuntu if anything is missing:

    sudo apt install gcc binutils strace gdb

## Program design

Struct:

    struct transaction {
        int id;       // transaction ID
        int amount;   // transaction amount
        char type;    // 'D' for deposit, 'W' for withdrawal
    };

The struct is 12 bytes (4 + 4 + 1 + 3 bytes of padding).

Global variable:

* total_processed: counts how many transactions have been processed.
  It starts at 0, so it is stored in the .bss section.

User defined functions (besides main):

1. fill_transactions(list, count)
   Writes the transaction data into the heap array.
2. process_transactions(list, count, deposits, withdrawals)
   Loops over the array, uses an if statement to separate deposits from
   withdrawals, adds up each total through pointers, and increments
   total_processed.
3. print_report(deposits, withdrawals)
   Calculates the final balance and prints the results with printf.

main() allocates the array with malloc (5 * sizeof(struct transaction) = 60
bytes), calls the three functions in order, and releases the memory with free.

## Compilation

Run these commands in the project folder.

Executable for static analysis and strace:

    gcc -Wall -O0 -fno-inline -o program program.c
    strip program

Executable for debugging with GDB:

    gcc -Wall -O0 -g -fno-inline -o program_debug program.c

Both commands should finish with no warnings or errors. Verify that the
executable is stripped:

    file program

The output should contain the word "stripped".

## Execution

    ./program

or, for the debug build:

    ./program_debug

## Inputs

The program takes no command line arguments and reads nothing from standard
input or from files. The transaction data is generated inside
fill_transactions:

| ID | Amount | Type       |
|----|--------|------------|
| 1  | 100    | Deposit    |
| 2  | 200    | Withdrawal |
| 3  | 300    | Deposit    |
| 4  | 400    | Deposit    |
| 5  | 500    | Withdrawal |

## Expected output

    Transactions processed: 5
    Total deposits:         800
    Total withdrawals:      700
    Final balance:          100

Deposits are 100 + 300 + 400 = 800. Withdrawals are 200 + 500 = 700. The
final balance is 800 - 700 = 100. The program exits with status 0. If malloc
fails, the program prints nothing and exits with status 1.

Check the exit status with:

    echo $?

## Analysis commands

Part B, static analysis (run on the stripped program):

    readelf -h program                 ELF header: architecture, type, entry point
    readelf -S program                 Section headers
    readelf -l program                 Program headers and interpreter
    readelf -d program                 Dynamic section (NEEDED, BIND_NOW)
    readelf -r program                 Relocations (GOT slots for library functions)
    objdump -d -M intel program        Full disassembly
    objdump -d -M intel -j .plt.sec program    PLT stubs

Part C, dynamic analysis:

    strace ./program
    strace -o c_strace.txt ./program   Save the trace to a file

Part D, debugging (run on program_debug):

    gdb ./program_debug
    (gdb) break _start
    (gdb) break main
    (gdb) break process_transactions
    (gdb) run
    (gdb) continue                     Repeat until process_transactions is reached
    (gdb) bt                           Call stack
    (gdb) print total_processed        Global variable
    (gdb) print *list@5                Heap records
    (gdb) x/12xb list                  Raw bytes of the first record
    (gdb) print &count                 Stack variable address
    (gdb) finish                       Run until the function returns
    (gdb) info proc mappings           Memory regions (stack, heap, program)

## Notes

* The executable is a position independent executable (PIE). Addresses in
  readelf and objdump are offsets. Under GDB the base address is
  0x555555554000, so main at offset 0x13c6 appears at 0x5555555553c6.
* strace runs with address randomization enabled, so its addresses change
  between runs. The offsets in Part B do not change.
* The stripped program has no function names. In the report, main and the
  three user functions are identified from the call order and arguments in
  the disassembly.
