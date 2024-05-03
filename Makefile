CC      := gcc
LIB     := -lpthread $(shell pkg-config --libs notcurses-core) $(shell pkg-config --libs ncursesw)
CFLAGS  := $(shell pkg-config --cflags notcurses-core) $(shell pkg-config --libs ncursesw)
#Put all names of .config files into TARGETS variable after removing the .config
TARGETS := $(patsubst %.config.c,%,$(wildcard *.config.c))

all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.config.c include.h show.h
	$(CC) $< -o $@ $(LIB) $(CFLAGS)
