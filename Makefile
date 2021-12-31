CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -std=c99
LFLAGS=-lm

build: main.c ini.c
	$(CC) $(CFLAGS) -o ctxt $^ $(LFLAGS)
