#define USE_NOTCURSES
#define BACKGROUND "background.png"
#define BACKGROUND_BLIT NCBLIT_DEFAULT

#include "include.h"

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct rule rules[] = {
//	                  |  -1 for auto  |
//	                   \_______       |
//	                           |      |
//	function           y   x    h    w  time (s) NULL
	timedate,         30,130,   1,  23,       1, NULL, "%Y-%m-%d %a %H:%M:%S",
	external_command, 31,120,   8,  42,       5, NULL, "cal -m -n 2 --color=never",
};

#include "show.h"
