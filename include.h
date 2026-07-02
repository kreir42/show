#define _GNU_SOURCE //needed for memfd_create

#define CURRENT_VERSION "0.1"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>
#include <signal.h>

#ifdef USE_NOTCURSES
#include <notcurses/notcurses.h>
extern struct notcurses* nc;
static int pixel_support = 0; //>0 once startup confirms the terminal can blit pixel graphics
#define BOX_HLINE "─"
#define BOX_VLINE "│"
#define BOX_URCORNER "┐"
#define BOX_ULCORNER "┌"
#define BOX_LRCORNER "┘"
#define BOX_LLCORNER "└"
#else
#include <ncurses.h>
#include <sys/ioctl.h>
#endif

#include <unistd.h>

#ifdef USE_NOTCURSES
typedef struct ncplane* window_handle_t;
#else
typedef WINDOW* window_handle_t;
#endif

//placement system: each axis is positioned by anchoring one point of this widget to one point of a reference (the screen or another widget) plus an offset
enum{ START=0, CENTER, END }; //a point along an axis: top/left, middle, or bottom/right edge

//reference target for an anchor or a matched size
#define SCREEN	0 //default so an omitted anchor means "top-left at the screen's top-left"
#define WIDGET(i)	((i)+1) //WIDGET(i) references the widget at index i in widgets[] (use an enum of names for readability — see the configs).

//placement along one axis
struct anchor{
	int ref;		//SCREEN or WIDGET(index)
	char ref_point;		//START|CENTER|END on the reference
	char self_point;	//START|CENTER|END on this widget
	float offset;		//cells, or a fraction of the screen's axis if rel!=0
	char rel;		//if !=0, offset is a fraction of the screen along this axis
};

//size along one axis
enum{ SZ_ABS=0, SZ_REL, SZ_MATCH }; //absolute cells, fraction of screen, or equal to a reference's size
struct extent{
	char mode;		//SZ_ABS|SZ_REL|SZ_MATCH
	float value;		//cells (SZ_ABS) or fraction of screen (SZ_REL). ignored for SZ_MATCH
	int ref;		//WIDGET(index) whose size to match, if mode==SZ_MATCH
};

struct widget{
	void* (*widget)(void* input);	//the widget's thread function
	struct anchor y, x; //placement of the top and left edge
	struct extent h, w; //height and width
	int time;
	window_handle_t window;
	int_least16_t flags;
		#define DRAW_BOX		(1<<0)
		#define OPAQUE			(1<<1)
		#define BLEND_BACKGROUND	(1<<2)
		#define BOLD			(1<<3)
		#define ITALIC			(1<<4)
	void* data;
};

struct widget_geom{ int y, x, h, w; }; //resolved absolute geometry, computed once per layout by process_widgets

#ifdef USE_NOTCURSES
void draw_box(struct ncplane* plane){
	unsigned int h, w;
	ncplane_dim_yx(plane, &h, &w);
	h+=2; w+=2;
	struct ncplane_options plane_options = {
		.y = -1, .x = -1,
		.rows = h, .cols = w,
	};
	struct ncplane* box_plane = ncplane_create(plane, &plane_options);
	ncplane_move_below(box_plane, plane);
	nccell base_cell = NCCELL_TRIVIAL_INITIALIZER;
	ncplane_base(plane, &base_cell);
	ncplane_set_base_cell(box_plane, &base_cell);
	if(nccell_bg_alpha(&base_cell) == NCALPHA_BLEND){
		nccell_set_bg_alpha(&base_cell, NCALPHA_TRANSPARENT);
		ncplane_set_base_cell(plane, &base_cell);
	}

	nccell ul = NCCELL_TRIVIAL_INITIALIZER, ur = NCCELL_TRIVIAL_INITIALIZER;
	nccell ll = NCCELL_TRIVIAL_INITIALIZER, lr = NCCELL_TRIVIAL_INITIALIZER;
	nccell hl = NCCELL_TRIVIAL_INITIALIZER, vl = NCCELL_TRIVIAL_INITIALIZER;

	nccell_load(box_plane, &ul, BOX_ULCORNER); nccell_load(box_plane, &ur, BOX_URCORNER);
	nccell_load(box_plane, &ll, BOX_LLCORNER); nccell_load(box_plane, &lr, BOX_LRCORNER);
	nccell_load(box_plane, &hl, BOX_HLINE);    nccell_load(box_plane, &vl, BOX_VLINE);

	ncplane_perimeter(box_plane, &ul, &ur, &ll, &lr, &hl, &vl, 0);

	nccell_release(box_plane, &ul); nccell_release(box_plane, &ur);
	nccell_release(box_plane, &ll); nccell_release(box_plane, &lr);
	nccell_release(box_plane, &hl); nccell_release(box_plane, &vl);
}
#else
//create a border window one cell larger than "window" on every side and draw the box on its perimeter. returns the new window, or NULL if it would fall off-screen
WINDOW* draw_box(WINDOW* window){
	int y, x, h, w;
	getbegyx(window, y, x);
	getmaxyx(window, h, w);
	WINDOW* box_window = newwin(h+2, w+2, y-1, x-1); //surrounds the content window on every side
	if(box_window == NULL) return NULL; //doesn't fit on screen
	box(box_window, 0, 0); //ACS_VLINE/ACS_HLINE sides with the default ACS corners
	return box_window;
}
#endif

//report the widget's size as resolved by process_widgets at the last layout. defined in show.h
static void get_size(struct widget* widget, int* h, int* w);

//both backends are unsafe under concurrent drawing and must serialize through this mutex: ncurses is not thread-safe at all, and a notcurses plane can't be mutated while its pile is being rendered. the render loop in update_function holds it across a render, and every widget that draws holds it across each drawing operation
//cancellation is disabled while it's held so a thread can't be cancelled mid-draw while holding the lock, which would leave it locked and cause a deadlock
//widgets that draw through draw_string/stage_refresh get this for free, widgets that call the backend directly must wrap their drawing in draw_lock/draw_unlock
static pthread_mutex_t draw_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int draw_cancelstate; //per-thread saved cancel state
static inline void draw_lock(void){
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &draw_cancelstate);
	pthread_mutex_lock(&draw_mutex);
}
static inline void draw_unlock(void){
	pthread_mutex_unlock(&draw_mutex);
	pthread_setcancelstate(draw_cancelstate, NULL);
}

//report the widget's size in pixels: cells times the terminal's per-cell pixel dimensions. used for the {{pw}}/{{ph}} placeholders of the dynamic_external_command widgets
static void get_pixel_size(struct widget* widget, int* ph, int* pw){
	int h, w;
	get_size(widget, &h, &w);
#ifdef USE_NOTCURSES
	unsigned pxy, pxx, celldimy, celldimx, maxbmapy, maxbmapx;
	draw_lock(); //serialize the notcurses read against the render loop
	ncplane_pixel_geom(widget->window, &pxy, &pxx, &celldimy, &celldimx, &maxbmapy, &maxbmapx);
	draw_unlock();
	*ph = h * (int)celldimy;
	*pw = w * (int)celldimx;
#else
	int cellh = 0, cellw = 0;
	struct winsize ws;
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0){
		cellh = ws.ws_ypixel / ws.ws_row;
		cellw = ws.ws_xpixel / ws.ws_col;
	}
	if(cellh <= 0) cellh = 16; //fallback when the terminal doesn't report pixel geometry over TIOCGWINSZ
	if(cellw <= 0) cellw = 8;
	*ph = h * cellh;
	*pw = w * cellw;
#endif
}

static inline void stage_refresh(struct widget* widget){
#ifdef USE_NOTCURSES
	(void)widget; //suppress warning, nothing to stage: the render loop picks up plane changes on its own
#else
	draw_lock();
	wnoutrefresh(widget->window);
	draw_unlock();
#endif
}

static inline void draw_string(struct widget* widget, int y, int x, const char* str){
	draw_lock();
#ifdef USE_NOTCURSES
	ncplane_putstr_yx(widget->window, y, x, str);
#else
	mvwaddstr(widget->window, y, x, str);
#endif
	draw_unlock();
}

//in a just-forked child, restore the default empty signal mask before exec
static inline void reset_child_sigmask(void){
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);
}

#include "big_font.h" //BIG_FONT_H, BIG_FONT_W and the glyph table

//return wether the pixel at native coordinate (ny,nx) of the rendered string is set. glyphs are BIG_FONT_W wide with a 1-pixel gap between them. unknown characters render blank
static int big_font_pixel(const char* str, int ny, int nx){
	int glyph = nx/(BIG_FONT_W+1); //which character
	int col = nx%(BIG_FONT_W+1); //which column within that character's columns
	if(col>=BIG_FONT_W) return 0; //inter-glyph gap column
	unsigned char c = str[glyph];
	if(c>='a'&&c<='z') c -= 32; //fold lowercase to uppercase
	if(c>=128) return 0; //non-ASCII render blank
	return (big_font[c][ny]>>(BIG_FONT_W-1-col))&1;
}

//render a string (only digits and ':') in block letters scaled to fill the widget's h x w using half-block glyphs
static void draw_big_string(struct widget* widget, const char* str){
	int h, w;
	get_size(widget, &h, &w);
	int n = strlen(str);
	if(n<1) return;
	int native_w = n*(BIG_FONT_W+1) - 1; //no trailing gap after the last glyph
	int px_h = h*2; //two vertical pixels per cell row
	size_t rowsize = (size_t)w*3 + 1; //up to 3 UTF-8 bytes per block glyph, +1 for NUL
	char* row = malloc(rowsize);
	if(!row) return;
	pthread_cleanup_push(free, row); //free on thread cancel
	//draw row by row
	for(int r=0; r<h; r++){
		int idx = 0;
		int top_py = (2*r)  *BIG_FONT_H/px_h; //cell's top half
		int bot_py = (2*r+1)*BIG_FONT_H/px_h; //cell's bottom half
		for(int c=0; c<w; c++){ //iterate output cells in row
			int nx = c*native_w/w; //corresponding pixel column
			int top = big_font_pixel(str, top_py, nx);
			int bot = big_font_pixel(str, bot_py, nx);
			const char* glyph;
			if(top&&bot)  glyph = "█";
			else if(top)  glyph = "▀";
			else if(bot)  glyph = "▄";
			else          glyph = " ";
			size_t len = strlen(glyph);
			memcpy(row+idx, glyph, len); //append glyph's bytes
			idx += len;
		}
		row[idx] = '\0';
		draw_string(widget, r, 0, row); //draw entire row
	}
	stage_refresh(widget);
	pthread_cleanup_pop(1); //frees row, balances the push macro
}

//include every header in widgets/ using widgets.h, generated by make
#include "widgets.h"
