#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "CHECK(" #cond ") failed at %s:%d\n", __FILE__, __LINE__); \
        exit(-1); \
    }

int main() {
	FILE *db, *ref;
dsf
	CHECK(1 == 0);

	db = fopen("/dev/db", "rw");

	return 0;
}
