CC      := gcc
LIB     := -lncursesw -lpthread
#Put all names of .c files into TARGETS variable after removing the .c
TARGETS := $(patsubst %.c,%,$(wildcard *.c))

all: $(TARGETS)

clean: $(TARGETS)
	rm $(TARGETS)

%: %.c
	$(CC) $< -o $@ $(LIB)
