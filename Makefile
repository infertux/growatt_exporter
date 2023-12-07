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
	clang-format --verbose --Werror -i --style=file src/*.{c,h} tests/*.c
	clang-tidy --checks='*,-altera-id-dependent-backward-branch,-altera-unroll-loops,-bugprone-assignment-in-if-condition,-cert-err33-c,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-cppcoreguidelines-avoid-magic-numbers,-llvm-header-guard,-llvmlibc-restrict-system-libc-headers,-readability-function-cognitive-complexity' --format-style=llvm src/*.{c,h} tests/*.c -- $(CFLAGS)
.PHONY: lint

test: growatt tests/*.c
	$(CC) -Wall -Werror -o tests/mock-server tests/*.c $(shell pkg-config --libs --cflags libmodbus)
	./tests/mock-server &
	timeout 30 mosquitto_sub -h test.mosquitto.org -p 1884 -u rw -P readwrite -t homeassistant/sensor/growatt/state -d &
	./growatt 127.0.0.1:1502 --prometheus 1234 --mqtt test.mosquitto.org 1884 wo writeonly

clean:
	$(RM) growatt tests/mock-server
