CC := gcc
LIB := -lpthread
CFLAGS := 
#Put all names of .config files into TARGETS variable after removing the .config
TARGETS := $(patsubst %.config.c,%,$(wildcard *.config.c))

NOTCURSES_LIB := $(shell pkg-config --libs notcurses)
NOTCURSES_CFLAGS := $(shell pkg-config --cflags notcurses)

NCURSES_LIB := $(shell pkg-config --libs ncursesw)
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw)


all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.config.c include.h show.h
	@if grep -q "^[^/]*#define USE_NOTCURSES" $<; then\
		echo "Compiling $< with notcurses";\
		$(CC) $< -o $@ $(LIB) $(CFLAGS) $(NOTCURSES_LIB) $(NOTCURSES_CFLAGS);\
	else\
		echo "Compiling $< with ncurses";\
		$(CC) $< -o $@ $(LIB) $(CFLAGS) $(NCURSES_LIB) $(NCURSES_CFLAGS);\
	fi
