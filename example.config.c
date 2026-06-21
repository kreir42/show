#define USE_NOTCURSES

#include "include.h"

#define PROGRAM_LOCALE ""
#define REFRESH_MICROSECONDS 200000

static struct widget widgets[] = {
	{
		.widget = image, .data = "example_background.png", .time = 1000,
		.h = {SZ_REL, 1}, .w = {SZ_REL, 1}, //fills the screen
		.y = {.self_point = CENTER, .ref_point = CENTER},
		.x = {.self_point = CENTER, .ref_point = CENTER},
		.flags = OPAQUE,
	},
	{
		.widget = print_string, .data = "漢字でもできる!",
		.h = {SZ_ABS, 1}, .w = {SZ_ABS, 15},
		.x = {.self_point = END, .ref_point = END},
	},
	{
		.widget = large_clock, .data = "%H:%M:%S", .time = 1,
		.h = {SZ_REL, 0.3}, .w = {SZ_REL, 0.8},
		.y = {.offset = 0.2, .rel = 1},
		.x = {.self_point = CENTER, .ref_point = CENTER},
	},
	{
		.widget = timedate, .data = "%Y-%m-%d %a %H:%M:%S", .time = 1,
		.h = {SZ_ABS, 1}, .w = {SZ_ABS, 23},
		.y = {.ref = WIDGET(2), .ref_point = END, .offset = 1}, //1 row below the clock
		.x = {.self_point = CENTER, .ref_point = CENTER},
		.flags = BOLD,
	},
	{
		.widget = print_large_string, .data = "show",
		.h = {SZ_REL, 0.1}, .w = {SZ_ABS, 50},
		.y = {.self_point=END, .ref_point = END},
		.x = {.self_point=END, .ref_point = END, .offset = -1},
	},
	{
		.widget = external_command, .data = "cal -m -n 2 --color=always", .time = 6*3600,
		.h = {SZ_ABS, 8}, .w = {SZ_ABS, 42},
		.y = {.self_point = END, .ref_point = END, .offset = -1},
		.x = {.self_point = START, .ref_point = START, .offset = 1},
		.flags = DRAW_BOX | BLEND_BACKGROUND,
	},
	{
		.widget = live_external_command, .data = "top",
		.h = {SZ_ABS, 14}, .w = {SZ_ABS, 80},
		.y = {.offset = 0.6, .rel = 1},
		.x = {.self_point = CENTER, .ref_point = CENTER},
		.flags = DRAW_BOX | BLEND_BACKGROUND,
	},
};

#include "show.h"
