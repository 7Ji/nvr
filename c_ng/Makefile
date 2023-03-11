BINARY = nvr
CC ?= gcc
STRIP ?= strip
CFLAGS = -Wall -Wextra
STATIC ?= 0
DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CFLAGS += -g -DDEBUGGING
endif
ifeq ($(STATIC), 1)
	LDFLAGS += -static
endif

_OBJECTS = $(wildcard *.c)
OBJECTS = $(patsubst %.c,%.o,$(_OBJECTS))

$(BINARY): $(OBJECTS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
ifneq ($(DEBUG), 1)
	$(STRIP) $(BINARY)
endif

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: fresh clean

fresh: clean $(BINARY)

clean:
	rm -f *.o $(BINARY)