#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

const size_t TEST_AREA_LENGTH = 100000;

#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(" #cond ") failed\n", __FILE__, __LINE__); \
        exit(-1); \
    }

int main() {
	FILE *db, *ref;

	db = fopen("/dev/db", "r+");
	CHECK(db != NULL);

	ref = fopen("ref.txt", "w+");
	CHECK(ref != NULL);

	// Clear out the ref file. Might be stale.
	for (int i = 0; i < TEST_AREA_LENGTH; i++) {
		fputc(0, ref);
	}

	return 0;
}
