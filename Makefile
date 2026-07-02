CC := gcc
LIB := -lpthread -lvterm -lm
CFLAGS := -Wall -Wextra -O2 -Wno-clobbered
#-Wno-clobbered: suppress spurious "might be clobbered by longjmp" warnings inside pthread_cleanup_push macros
#Put all names of .config files into TARGETS variable after removing the .config
TARGETS := $(patsubst %.config.c,%,$(wildcard *.config.c))

NOTCURSES_LIB := $(shell pkg-config --libs notcurses)
NOTCURSES_CFLAGS := $(shell pkg-config --cflags notcurses)

NCURSES_LIB := $(shell pkg-config --libs ncursesw)
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw)

#Every header in widgets/ is auto-included into include.h via the generated widgets.h
WIDGET_HEADERS := $(wildcard widgets/*.h)
#Per-type plot headers are NOT auto-included, but list them so editing one still triggers a rebuild
PLOT_HEADERS := $(wildcard widgets/plot/*.h)


.PHONY: all clean

all: $(TARGETS)

clean:
	rm -f $(TARGETS) widgets.h

#Generate the auto-include list: one #include line per widgets/*.h header
widgets.h: $(WIDGET_HEADERS)
	@echo "Generating $@"
	@printf '#include "%s"\n' $(WIDGET_HEADERS) > $@

%: %.config.c widgets.h *.h $(PLOT_HEADERS)
	@if grep -q "^[^/]*#define USE_NOTCURSES" $<; then\
		echo "Compiling $< with notcurses";\
		$(CC) $< -o $@ $(LIB) $(CFLAGS) $(NOTCURSES_LIB) $(NOTCURSES_CFLAGS);\
	else\
		echo "Compiling $< with ncurses";\
		$(CC) $< -o $@ $(LIB) $(CFLAGS) $(NCURSES_LIB) $(NCURSES_CFLAGS);\
	fi
