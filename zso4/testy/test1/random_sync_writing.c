#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(" #cond ") failed\n", __FILE__, __LINE__); \
        exit(-1); \
    }

int main() {
	FILE *db, *ref;

	CHECK(1 == 0);

	db = fopen("/dev/db", "rw");

	return 0;
}
