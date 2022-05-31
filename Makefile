setup:
	gcc --std=gnu99 -g -Wall -o smallsh main.c

clean:
	rm smallsh

debug:
	valgrind --leak-check=yes --show-reachable=yes ./smallsh

test:
	./p3testscript 2>&1
