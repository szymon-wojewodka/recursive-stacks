.PHONY: all clean

CC = gcc
CFLAGS = -Wall -Wextra -Wno-implicit-fallthrough -std=gnu23 -fPIC -O2
LDFLAGS = -shared \
	-Wl,--wrap=malloc \
	-Wl,--wrap=calloc \
	-Wl,--wrap=realloc \
	-Wl,--wrap=reallocarray \
	-Wl,--wrap=free \
	-Wl,--wrap=strdup \
	-Wl,--wrap=strndup

OBJS = rstack.o memory_tests.o

librstack.so: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

rstack.o: rstack.c rstack.h memory_tests.h
	$(CC) $(CFLAGS) -c -o $@ $<

memory_tests.o: memory_tests.c memory_tests.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) librstack.so
