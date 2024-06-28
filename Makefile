CC?=clang
#CC?=gcc
RM=rm -fv
CFLAGS=$(shell pkg-config --cflags libbsd libconfig libmodbus libmosquitto)
LIBS=$(shell pkg-config --libs libbsd libconfig libmodbus libmosquitto) -pthread
SRCS=src/*
TESTS=tests/*.c

all: growatt_exporter

doc: $(SRCS)
	doxygen .doxygen

growatt_exporter: $(SRCS)
	$(CC) -v $(CFLAGS) $(LIBS) -Wall -Werror -O3 -o growatt_exporter src/*.c

lint:
	clang-format --verbose --Werror -i --style=file $(SRCS) $(TESTS)
	clang-tidy --checks='*,-altera-id-dependent-backward-branch,-altera-unroll-loops,-bugprone-assignment-in-if-condition,-cert-err33-c,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-cppcoreguidelines-avoid-magic-numbers,-llvm-header-guard,-llvmlibc-restrict-system-libc-headers,-readability-function-cognitive-complexity' --format-style=llvm $(SRCS) $(TESTS) -- $(CFLAGS)
.PHONY: lint

test: growatt_exporter $(TESTS)
	$(CC) -v $(shell pkg-config --libs --cflags libbsd libmodbus) -Wall -Werror -o tests/mock-server $(TESTS)
	timeout 30 mosquitto_sub -h test.mosquitto.org -p 1884 -u rw -P readwrite -t homeassistant/sensor/growatt/state -d &
	./tests/mock-server &
	./growatt_exporter config-example.conf || true

clean:
	$(RM) growatt_exporter tests/mock-server
