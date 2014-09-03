/*
 * transdb.h
 *
 * Author: Przemyslaw Horban <p.horban@mimuw.edu.pl>
 */

#ifndef TRANSDB_H_
#define TRANSDB_H_

#define _TRANSDB_IO_MAGIC 's'

#define DB_COMMIT _IO(_TRANSDB_IO_MAGIC, 0x31)
#define DB_ROLLBACK _IO(_TRANSDB_IO_MAGIC, 0x32)

#endif /* TRANSDB_H_ */
