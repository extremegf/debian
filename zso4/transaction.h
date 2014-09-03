/*
 * transaction.h
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#ifndef TRANSACTION_H_
#define TRANSACTION_H_

/* Controlls the size of DB segments. Segment is a minimal
 * locking unit for the db. If two transactions read and write to
 * a single semgment, then one of them will fail.
 * Increasing the segment size reduces memory and computational
 * overheads, at the cost of more rollbacks.
 */
#define SEGMENT_SIZE 1  // For maximum compatibility set to 1


// Each commit adds to the database versions chain. Read time is
// proportional to the chain length. Often it can be shrunken
// when many of it's links have no pending-transaction parents.
// This constants regulates how often this is done. 
#define COMMITS_BEFORE_COMAPCTION 15

struct trans_context_t;

int trans_init(void);
void trans_destroy(void);

#endif /* TRANSACTION_H_ */
