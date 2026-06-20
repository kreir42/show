#define USE_NOTCURSES

#include "include.h"

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct rule rules[] = {
//	function                 y     x      h      w   time (s) NULL  flags                                              argument
	image,                   0,    0,     1,     1,     1000, NULL, CENTER|RELATIVE_SIZE|OPAQUE,                       "example_background.png",
	print_string,            0,    0,     1,    34,        0, NULL, CENTER_X,                                          "EXAMPLE WITH WIDE CHARACTERS: 漢字",
	timedate,              0.2,    0,     1,    23,        1, NULL, CENTER_X|RELATIVE_Y_POS|BOLD,                      "%Y-%m-%d %a %H:%M:%S",
	large_clock,           0.2,    0,   0.3,   0.8,        1, NULL, CENTER_X|RELATIVE_POS|RELATIVE_SIZE,               "%H:%M:%S",
	print_large_string,      0,0.925,  0.15, 0.075,        0, NULL, RELATIVE_POS|RELATIVE_SIZE,                        ":/",
	external_command,        0,    0,     8,    42,   6*3600, NULL, CENTER|DRAW_BOX|BLEND_BACKGROUND,                  "cal -m -n 2 --color=always",
	live_external_command, 0.6,    0,    14,    80,        0, NULL, CENTER_X|RELATIVE_Y_POS|DRAW_BOX|BLEND_BACKGROUND, "top",
};

#include "show.h"
