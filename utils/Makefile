all: filter

CFLAGS=-DHAVE_RDTSCP -O3 -mtune=native
LDFLAGS=-lm

filter: filter.c
	$(CC) filter.c $(CFLAGS) $(LDFLAGS) -o filter

clean:
	rm filter
