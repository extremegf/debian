#include <stdio.h>

#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "CHECK(" #cond ") failed at %s:%d", __FILE__:__LINE__); \
        exit(-1); \
    }

int main() {
	FILE *db, *ref;

	CHECK(1 == 0);

	db = fopen("/dev/db", "rw");

	return 0;
}
