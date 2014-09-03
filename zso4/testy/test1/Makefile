all: random_sync_writing test1


random_sync_writing: random_sync_writing.c
	gcc -O2 -g random_sync_writing.c -o random_sync_writing
	

test1: random_sync_writing
	./random_sync_writing