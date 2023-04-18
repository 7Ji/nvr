BINARY = nvr
DIR_INCLUDE = include
DIR_SOURCE = src
DIR_OBJECT = obj
CC ?= gcc
STRIP ?= strip
LDFLAGS = -lavformat -lavutil -lavcodec
CFLAGS = -I$(DIR_INCLUDE) -Wall -Wextra
STATIC ?= 0
DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CFLAGS += -g -DDEBUGGING
endif
ifeq ($(STATIC), 1)
	LDFLAGS += -static
endif

INCLUDES = $(wildcard $(DIR_INCLUDE)/*.h)

_SRCS = $(wildcard $(DIR_SOURCE)/*.c)

OBJECTS = $(patsubst $(DIR_SOURCE)/%.c,$(DIR_OBJECT)/%.o,$(_SRCS))

ifndef VERSION
	VERSION_GIT_TAG := $(shell git describe --abbrev=0 --tags ${TAG_COMMIT} 2>/dev/null || true)
	VERSION_GIT_TAG_NO_V := $(VERSION_GIT_TAG:v%=%)
	VERSION_GIT_COMMIT := $(shell git rev-list --abbrev-commit --tags --max-count=1)
	VERSION_GIT_DATE := $(shell git log -1 --format=%cd --date=format:"%Y%m%d")
	VERSION := $(VERSION_GIT_TAG_NO_V)-$(VERSION_GIT_COMMIT)-$(VERSION_GIT_DATE)
	GIT_STAT := $(shell git diff --stat)
	ifeq ($(VERSION),--)
		undefine VERSION
	else
		ifneq ($(GIT_STAT),)
			VERSION := $(VERSION)-DIRTY
		endif
	endif
endif

$(BINARY): $(OBJECTS) | prepare
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
ifneq ($(DEBUG), 1)
	$(STRIP) $(BINARY)
endif

ifdef VERSION
$(DIR_OBJECT)/version.o: $(DIR_SOURCE)/version.c $(INCLUDES) version | prepare
	$(CC) -c -o $@ $< $(CFLAGS) -DVERSION=\"$(VERSION)\"
endif

$(DIR_OBJECT)/%.o: $(DIR_SOURCE)/%.c $(INCLUDES) | prepare
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: fresh clean prepare version

fresh: clean $(BINARY)

clean:
	rm -rf $(DIR_OBJECT) $(BINARY)

prepare:
	mkdir -p $(DIR_OBJECT)