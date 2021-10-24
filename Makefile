CC=clang
RM=rm -f
CFLAGS=$(shell pkg-config --cflags libmodbus)
LIBS=$(shell pkg-config --libs libmodbus)

SRCS=src/epever.c src/*.h

all: epever lint

epever: $(SRCS)
	$(CC) $(CFLAGS) $(LIBS) -Wall -Werror -pedantic -o epever src/epever.c

lint:
	clang-format --verbose --Werror -i --style=llvm src/*
	clang-tidy --checks='*,-llvmlibc-restrict-system-libc-headers' --format-style=llvm src/* -- $(CFLAGS)
.PHONY: lint

clean:
	$(RM) epever
