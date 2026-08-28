CC      = gcc
CFLAGS  = -Wall -Wextra -g -std=c99
LDFLAGS = -lm

.PHONY: all test clean

all: test_botnav

test_botnav: test_botnav.c g_newbotai.c g_newbotai.h
	$(CC) $(CFLAGS) -o $@ test_botnav.c g_newbotai.c $(LDFLAGS)

test: test_botnav
	./test_botnav

clean:
	rm -f test_botnav
