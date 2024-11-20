#define USE_NOTCURSES
#define BACKGROUND "example_background.png"
#define BACKGROUND_BLIT NCBLIT_DEFAULT

#include "include.h"

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct rule rules[] = {
//	function             y     x      h      w   time (s) NULL  flags                    argument
	print_string,        0,    0,     1,    34,        0, NULL, CENTER_X,                "EXAMPLE WITH WIDE CHARACTERS: 漢字",
	timedate,          0.2,    0,     1,    23,        1, NULL, CENTER_X|RELATIVE_Y_POS, "%Y-%m-%d %a %H:%M:%S",
	external_command,    0,    0,     8,    42,   6*3600, NULL, CENTER|DRAW_BOX,         "cal -m -n 2 --color=never",
	print_string,        0,    0,     1,     9,        0, NULL, 0,                       "CPU usage",
	plot,                0,    0,    20,    40,        1, NULL, DRAW_BOX,                "top -bn1 | grep \"Cpu(s)\" | sed \"s/.*, *\\([0-9.]*\\)%* id.*/\\1/\" | awk '{print 100 - $1}'",
};

#include "show.h"
