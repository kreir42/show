CC      := gcc
LIB     := -lpthread $(shell pkg-config --libs notcurses)
CFLAGS  := $(shell pkg-config --cflags notcurses)
#Put all names of .c files into TARGETS variable after removing the .c
TARGETS := $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.c include.h
	$(CC) $< -o $@ $(LIB) $(CFLAGS)
