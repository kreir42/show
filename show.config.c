#include "include.h"

//#define USE_NOTCURSES
#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct rule rules[] = {
//	                  |  -1 for auto  |
//	                   \_______       |
//	                           |      |
//	function           y   x    h    w  time (s) NULL
	timedate,          0, 10,   1,  23,       1, NULL, "%Y-%m-%d %a %H:%M:%S",
	external_command,  1,  0,   8,  42,       5, NULL, "cal -m -n 2 --color=never",
};

#include "show.h"
