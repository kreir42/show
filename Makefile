CC      := gcc
LIB     := -lncursesw -lpthread
#Put all names of .c files into TARGETS variable after removing the .c
TARGETS := $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

clean:
	rm $(TARGETS)

%: %.c include.h
	$(CC) $< -o $@ $(LIB)
