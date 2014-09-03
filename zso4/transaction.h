/*
 * transaction.h
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#ifndef TRANSACTION_H_
#define TRANSACTION_H_

/* Controls the size of DB segments. Segment is a minimal
 * locking unit for the db. If two transactions read and write to
 * a single segment, then one of them will fail.
 * Increasing the segment size reduces memory and computational
 * overheads, at the cost of more rollbacks.
 */
#define SEGMENT_SIZE 1  // For maximum compatibility set to 1


// Each commit adds to the database versions chain. Read time is
// proportional to the chain length. Often it can be shrunken
// when many of it's links have no pending-transaction parents.
// This constants regulates how often this is done.
#define COMMITS_BEFORE_COMAPCTION 15

typedef enum { COMMIT, ROLLBACK } trans_result_t;

struct trans_context_t;

int trans_init(void);
void trans_destroy(void);

struct trans_context_t *new_trans_context(void);
char *get_write_segment(struct trans_context_t *trans, size_t seg_nr);
char *get_read_segment(struct trans_context_t *trans, size_t seg_nr);
trans_result_t finish_transaction(trans_result_t result,
                                  struct trans_context_t *trans);

#endif /* TRANSACTION_H_ */
