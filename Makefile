CC      := gcc
LIB     := -lpthread $(shell pkg-config --libs notcurses-core) $(shell pkg-config --libs ncursesw)
CFLAGS  := $(shell pkg-config --cflags notcurses-core) $(shell pkg-config --libs ncursesw)
#Put all names of .c files into TARGETS variable after removing the .c
TARGETS := $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.c include.h
	$(CC) $< -o $@ $(LIB) $(CFLAGS)
