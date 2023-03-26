BINARY = nvr
CXX ?= g++
STRIP ?= strip
CFLAGS = -Wall -Wextra
STATIC ?= 0
DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CFLAGS += -g
endif
ifeq ($(STATIC), 1)
	LDFLAGS += -static
endif

_OBJECTS = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(_OBJECTS))

$(BINARY): $(OBJECTS)
	$(CXX) -o $@ $^ $(CFLAGS) $(LDFLAGS)
ifneq ($(DEBUG), 1)
	$(STRIP) $(BINARY)
endif

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o $(BINARY)