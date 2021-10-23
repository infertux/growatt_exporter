CC=clang
RM=rm -f
CFLAGS=$(shell pkg-config --cflags libmodbus)
LIBS=$(shell pkg-config --libs libmodbus)

SRCS=src/epever.c src/*.h

all: epever tidy

epever: $(SRCS)
	$(CC) $(CFLAGS) $(LIBS) -Wall -Werror -pedantic -o epever src/epever.c

tidy:
	clang-tidy --checks='*,-llvmlibc-restrict-system-libc-headers' --format-style=llvm src/* -- $(CFLAGS)
.PHONY: tidy

clean:
	$(RM) epever
