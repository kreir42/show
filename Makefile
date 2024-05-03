CC      := gcc
LIB     := -lpthread $(shell pkg-config --libs notcurses-core)
CFLAGS  := $(shell pkg-config --cflags notcurses-core)
#Put all names of .c files into TARGETS variable after removing the .c
TARGETS := $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.c include.h
	$(CC) $< -o $@ $(LIB) $(CFLAGS)
