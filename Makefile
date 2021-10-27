CC=clang
RM=rm -f
CFLAGS=$(shell pkg-config --cflags libbsd libmodbus)
LIBS=$(shell pkg-config --libs libbsd libmodbus) -pthread

SRCS=src/epever.c src/*.h

all: epever lint

epever: $(SRCS)
	$(CC) $(CFLAGS) $(LIBS) -Wall -Werror -pedantic -O3 -o epever src/epever.c

lint:
	clang-format --verbose --Werror -i --style=llvm src/*
	clang-tidy --checks='*,-llvm-header-guard,-llvmlibc-restrict-system-libc-headers,-readability-function-cognitive-complexity,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' --format-style=llvm src/* -- $(CFLAGS)
.PHONY: lint

clean:
	$(RM) epever
