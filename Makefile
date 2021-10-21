CC=gcc
RM=rm -f
LIBS=$(shell pkg-config --cflags --libs libmodbus)

SRCS=modbus.c http.c

all: epever

epever:
	$(CC) $(LIBS) -o epever modbus.c http.c

clean:
	$(RM) epever
