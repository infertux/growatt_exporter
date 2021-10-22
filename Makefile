CC=gcc
RM=rm -f
LIBS=$(shell pkg-config --cflags --libs libmodbus)

SRCS=modbus.c http.c

all: epever

epever: $(SRCS)
	$(CC) $(LIBS) -Wall -Werror -pedantic -o epever $(SRCS)

clean:
	$(RM) epever
