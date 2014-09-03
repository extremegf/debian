#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <iostream>

using namespace std;

const size_t TEST_AREA_LENGTH = 10;
const size_t TEST_COUNT = 100;
const size_t MAX_SPAN = 2;
const bool VERBOSE = 1;

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
	FILE *db, *ref;

	srand(0); // Derandomize.

	db = fopen("/dev/db", "wb+");
	CHECK(db != NULL);

	ref = fopen("ref.txt", "wb+");
	CHECK(ref != NULL);

	// Clear out the ref file. Might be stale.
	for (int i = 0; i < TEST_AREA_LENGTH; i++) {
		fputc(0, ref);
	}

	for (size_t test_nr = 0; test_nr < TEST_COUNT; test_nr++) {
		size_t pos, len;
		char *data, *ref_data;

		do {
		   pos = random() % TEST_AREA_LENGTH;
		   len = random() % MAX_SPAN;
		} while (pos + len > TEST_AREA_LENGTH);


		if (rand() % 2 == 0) {
			deb("Test %d write\n", test_nr);

			data = new char[len];
			for (size_t i = 0; i < len; i++) {
				data[i] = rand() % 0x100;
			}
			fseek(db, pos, SEEK_SET);
			fseek(ref, pos, SEEK_SET);

			CHECK_EQ(fwrite(data, 1, len, db), len);
			CHECK_EQ(fwrite(data, 1, len, ref), len);
			delete[] data;
		}
		else {
			deb("Test %d read\n", test_nr);
			data = new char[len];
			ref_data = new char[len];

			fseek(db, pos, SEEK_SET);
			fseek(ref, pos, SEEK_SET);

			CHECK_EQ(fread(data, 1, len, db), len);
			CHECK_EQ(fread(ref_data, 1, len, ref), len);

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
