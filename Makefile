.PHONY: all clean
CC = gcc
CFLAGS = -O2 -Wno-int-conversion -Wno-implicit-function-declaration -Wno-implicit-int

all: code

code: main.c buddy.c buddy.h utils.h
	$(CC) $(CFLAGS) -o code main.c buddy.c

clean:
	rm -f code test
