all: random_sync_writing test1


random_sync_writing: random_sync_writing.c do_it
	gcc -O2 -g random_sync_writing.c -o random_sync_writing
	

test1: random_sync_writing
	./random_sync_writing