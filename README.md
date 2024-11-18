
# show

Simple, suckless-inspired, configurable terminal dashboard.

Show the output of multiple external commands with different refresh intervals in a multi-threaded [notcurses](https://github.com/dankamongmen/notcurses) or ncurses session.
A `timedate` internal commands is also included.

Configure the program by copying and modifying the `example.config.c` file under a new name, then run `make` to build.
You can choose to use ncurses instead of notcurses by removing the `#define USE_NOTCURSES` line

## Requirements

+ ncurses or [notcurses](https://github.com/dankamongmen/notcurses)
+ pthreads
