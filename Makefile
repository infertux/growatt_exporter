CC=gcc
RM=rm -f
LIBS=$(shell pkg-config --cflags --libs libmodbus)

SRCS=modbus.c http.c

all: epever

epever: $(SRCS)
	$(CC) $(LIBS) -o epever $(SRCS)

clean:
	$(RM) epever
