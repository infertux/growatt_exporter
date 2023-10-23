CC=clang
#CC=gcc
RM=rm -fv
CFLAGS=$(shell pkg-config --cflags libbsd libmodbus libmosquitto)
LIBS=$(shell pkg-config --libs libbsd libmodbus libmosquitto) -pthread

SRCS=src/*.c src/*.h

all: growatt

doc: $(SRCS)
	doxygen .doxygen

growatt: $(SRCS)
	$(CC) $(CFLAGS) $(LIBS) -Wall -Werror -O3 -o growatt src/*.c

lint:
	clang-format --verbose --Werror -i --style=file src/* tests/*
	clang-tidy --checks='*,-altera-id-dependent-backward-branch,-altera-unroll-loops,-bugprone-assignment-in-if-condition,-cert-err33-c,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-llvm-header-guard,-llvmlibc-restrict-system-libc-headers,-readability-function-cognitive-complexity' --format-style=llvm src/* tests/* -- $(CFLAGS)
.PHONY: lint

test: growatt tests/*.c
	$(CC) -Wall -Werror -pedantic -o tests/mock-server tests/*.c $(shell pkg-config --libs --cflags libmodbus)
	./tests/mock-server &
	./growatt 127.0.0.1:1502 --prometheus 1234 --mqtt test.mosquitto.org 1884 wo writeonly

clean:
	$(RM) growatt tests/mock-server
