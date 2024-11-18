#define USE_NOTCURSES
#define BACKGROUND "example_background.png"
#define BACKGROUND_BLIT NCBLIT_DEFAULT

#include "include.h"

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct rule rules[] = {
//	function             y     x      h      w   time (s) NULL  flags                    argument
	timedate,          0.2,    0,     1,    23,        1, NULL, CENTER_X|RELATIVE_Y_POS, "%Y-%m-%d %a %H:%M:%S",
	external_command,    0,    0,     8,    42,   6*3600, NULL, CENTER|DRAW_BOX,         "cal -m -n 2 --color=never",
};

#include "show.h"
