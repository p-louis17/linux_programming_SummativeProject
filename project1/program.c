#include <stdio.h>
#include <stdlib.h>

#define NUM_TRANSACTIONS 5

/* One bank transaction. type is 'D' for deposit, 'W' for withdrawal. */
struct transaction {
    int id;
    int amount;
    char type;
};

/* Global variable: counts how many transactions have been processed.
   Initialized to 0, so it is placed in .bss, not .data. */
int total_processed = 0;

/* Function 1: fill the heap array with transaction data.
   list  : pointer to the heap memory allocated in main
   count : how many transactions to write */
void fill_transactions(struct transaction *list, int count)
{
    /* The string literal is stored in .rodata. Each character is the
       type of the transaction at the same index. */
    const char *types = "DWDDW";

    for (int i = 0; i < count; i++) {
        list[i].id = i + 1;               /* IDs: 1, 2, 3, 4, 5 */
        list[i].amount = (i + 1) * 100;   /* Amounts: 100, 200, 300, 400, 500 */
        list[i].type = types[i];          /* D, W, D, D, W */
    }
}

/* Function 2: loop over the transactions, separate deposits from
   withdrawals, and add up each total.
   list         : the heap array filled by fill_transactions
   count        : how many transactions are in the array
   deposits     : output, points to a variable in main's stack frame
   withdrawals  : output, points to a variable in main's stack frame */
void process_transactions(struct transaction *list, int count,int *deposits, int *withdrawals)
{
    /* Start from zero, so the caller does not have to initialize them. */
    *deposits = 0;
    *withdrawals = 0;

    for (int i = 0; i < count; i++) {
        if (list[i].type == 'D') {
            *deposits += list[i].amount;       /* deposit branch */
        } else {
            *withdrawals += list[i].amount;    /* withdrawal branch */
        }
        total_processed++;                     /* update the global */
    }
}

/* Function 3: calculate the final balance and print the results.
   Totals are passed by value, so this function only reads them.
   deposits    : total of all deposits
   withdrawals : total of all withdrawals */
void print_report(int deposits, int withdrawals)
{
    /* Local variable on the stack. */
    int balance = deposits - withdrawals;

    printf("Transactions processed: %d\n", total_processed);
    printf("Total deposits:         %d\n", deposits);
    printf("Total withdrawals:      %d\n", withdrawals);
    printf("Final balance:          %d\n", balance);
}

int main(void)
{
    /* These two live on main's stack frame. Their addresses are passed
       to process_transactions so it can write the totals back. */
    int deposits;
    int withdrawals;

    /* Heap allocation: room for 5 transactions (5 * 12 = 60 bytes). */
    struct transaction *list = malloc(NUM_TRANSACTIONS * sizeof(struct transaction));
    if (list == NULL) {
        return 1;   /* allocation failed, nothing to free */
    }

    fill_transactions(list, NUM_TRANSACTIONS);
    process_transactions(list, NUM_TRANSACTIONS, &deposits, &withdrawals);
    print_report(deposits, withdrawals);

    free(list);
    return 0;
}