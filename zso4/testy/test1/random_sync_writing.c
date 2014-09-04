#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <iostream>

using namespace std;

const size_t TEST_AREA_LENGTH = 10;
const size_t TEST_COUNT = 20;
const size_t MAX_SPAN = 2;
const bool VERBOSE = 1;
const bool ONLY_READS = 0;

#define deb(...) if(VERBOSE) { printf (__VA_ARGS__); }


#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(" #cond ") failed\n", __FILE__, __LINE__); \
        exit(-1); \
    }

#define CHECK_EQ(a, b) \
	if ((a) != (b)) { \
		cerr << __FILE__ << ':' << __LINE__ << ": CHECK_EQ(" \
				<< #a << ", " << #b << ") " << \
				int(a) << " != " << int(b) << " failed\n"; \
        exit(-1); \
	}

int main() {
	int dbf, ref;

	srand(0); // Derandomize.

	dbf = open("/dev/db", O_RDWR);
	CHECK(dbf != 0);

	ref = open("ref.txt", O_RDWR);
	CHECK(ref != 0);

	// Clear out the ref file. Might be stale.
	for (int i = 0; i < TEST_AREA_LENGTH; i++) {
		write(ref, "\0", 1);
	}

	for (size_t test_nr = 0; test_nr < TEST_COUNT; test_nr++) {
		size_t pos, len;
		char *data, *ref_data;

		do {
		   pos = random() % TEST_AREA_LENGTH;
		   len = 1 + random() % MAX_SPAN;
		} while (pos + len > TEST_AREA_LENGTH);

		deb("Picked pos = %d and len = %d\n", pos, len);

		if (!ONLY_READS && rand() % 2 == 0) {
			deb("Test %d write\n", test_nr);

			data = new char[len];
			for (size_t i = 0; i < len; i++) {
				data[i] = rand() % 0x100;
			}
			lseek(dbf, pos, SEEK_SET);
			lseek(ref, pos, SEEK_SET);

			CHECK_EQ(write(ref, data, len), len);
			CHECK_EQ(write(dbf, data, len), len);
			delete[] data;
		}
		else {
			deb("Test %d read\n", test_nr);
			data = new char[len];
			ref_data = new char[len];

			lseek(dbf, pos, SEEK_SET);
			lseek(ref, pos, SEEK_SET);

			CHECK_EQ(read(ref, ref_data, len), len);
			CHECK_EQ(read(dbf, data, len), len);

			for (size_t i = 0; i < len; i++) {
				CHECK_EQ(data[i], ref_data[i]);
			}

			delete[] data;
			delete[] ref_data;
		}

	}

	puts("OK");
	return 0;
}
