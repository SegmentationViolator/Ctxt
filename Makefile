CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -std=c99
LFLAGS=-lm

ctxt: main.c ini.c
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)
