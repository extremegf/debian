#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

const size_t TEST_AREA_LENGTH = 3;
const size_t TEST_COUNT = 2;
const size_t MAX_SPAN = 1;

#define CHECK(cond) \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(" #cond ") failed\n", __FILE__, __LINE__); \
        exit(-1); \
    }

int main() {
	FILE *db, *ref;

	db = fopen("/dev/db", "rb+");
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

		printf("test %d\n", test_nr);

		do {
		   pos = random() % TEST_AREA_LENGTH;
		   len = random() % MAX_SPAN;
		} while (pos + len >= TEST_AREA_LENGTH);


		if (rand() % 2 == 0) {
			// Write!
			data = new char[len];
			for (size_t i = 0; i < len; i++) {
				data[i] = rand() % 0x100;
			}
			fseek(db, pos, SEEK_SET);
			fseek(ref, pos, SEEK_SET);

			fwrite(data, 1, len, db);
			fwrite(data, 1, len, ref);
			delete[] data;
		}
		else {
			// Read!
			data = new char[len];
			ref_data = new char[len];

			fseek(db, pos, SEEK_SET);
			fseek(ref, pos, SEEK_SET);

			fread(data, 1, len, db);
			fread(ref_data, 1, len, ref);

			for (size_t i = 0; i < len; i++) {
				CHECK(data[i] == ref_data[i]);
			}

			delete[] data;
			delete[] ref_data;
		}

	}

	puts("OK");
	return 0;
}
