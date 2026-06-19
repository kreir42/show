#define CURRENT_VERSION "0.1"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>

#ifdef USE_NOTCURSES
#include <notcurses/notcurses.h>
extern struct notcurses* nc;
#define BOX_HLINE "─"
#define BOX_VLINE "│"
#define BOX_URCORNER "┐"
#define BOX_ULCORNER "┌"
#define BOX_LRCORNER "┘"
#define BOX_LLCORNER "└"
#else
#include <ncurses.h>
#include <signal.h>
#endif

#include <unistd.h>

#ifdef USE_NOTCURSES
typedef struct ncplane* window_handle_t;
#else
typedef WINDOW* window_handle_t;
#endif

struct rule{
	void* (*function)(void* input);	//function that will run the rule
	float y, x;	//top-left corner position
	float h, w;
	int time;
	window_handle_t window;
	int_least16_t flags;
		#define CENTER_Y	 	(1<<0)
		#define CENTER_X	 	(1<<1)
		#define RELATIVE_Y_POS	 	(1<<2)
		#define RELATIVE_X_POS	 	(1<<3)
		#define RELATIVE_Y_SIZE		(1<<4)
		#define RELATIVE_X_SIZE		(1<<5)
		#define DRAW_BOX		(1<<6)
		#define OPAQUE			(1<<7)
		#define BLEND_BACKGROUND	(1<<8)
		#define BOLD			(1<<9)
		#define ITALIC			(1<<10)
		#define CENTER		CENTER_Y|CENTER_X
		#define RELATIVE_POS	RELATIVE_Y_POS|RELATIVE_X_POS
		#define RELATIVE_SIZE	RELATIVE_Y_SIZE|RELATIVE_X_SIZE
	void* data;
};

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
void draw_box(WINDOW* window){
	int y, x, h, w;
	getbegyx(window, y, x);
	getmaxyx(window, h, w);
	y--; x--;
	h+=2; w+=2;
	//corners
	mvaddch(    y,     x, ACS_ULCORNER);
	mvaddch(    y, w-1+x, ACS_URCORNER);
	mvaddch(h-1+y,     x, ACS_LLCORNER);
	mvaddch(h-1+y, w-1+x, ACS_LRCORNER);
	//sides
	mvhline(    y,   1+x, ACS_HLINE, w-2);
	mvhline(h-1+y,   1+x, ACS_HLINE, w-2);
	mvvline(  1+y,     x, ACS_VLINE, h-2);
	mvvline(  1+y, w-1+x, ACS_VLINE, h-2);
}
#endif

static void get_size(struct rule* rule, int* h, int* w){
	unsigned int max_h, max_w;
#ifdef USE_NOTCURSES
	notcurses_stddim_yx(nc, &max_h, &max_w);
#else
	getmaxyx(stdscr, max_h, max_w);
#endif
	if(rule->flags&RELATIVE_Y_SIZE) *h = rule->h*max_h;
	else *h = rule->h;
	if(rule->flags&RELATIVE_X_SIZE) *w = rule->w*max_w;
	else *w = rule->w;
}

#ifndef USE_NOTCURSES
//ncurses is not thread-safe, so all ncurses access must be serialized through this mutex
//cancellation is disabled while it's held so a thread can't be cancelled mid-draw while holding the lock, which would leave it locked and cause a deadlock
//widgets that draw through draw_string/stage_refresh get this for free, widgets that call ncurses directly must wrap their drawing in draw_lock/draw_unlock
static pthread_mutex_t curses_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int curses_cancelstate; //per-thread state
static inline void draw_lock(void){
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &curses_cancelstate);
	pthread_mutex_lock(&curses_mutex);
}
static inline void draw_unlock(void){
	pthread_mutex_unlock(&curses_mutex);
	pthread_setcancelstate(curses_cancelstate, NULL);
}
#endif

static inline void stage_refresh(struct rule* rule){
#ifndef USE_NOTCURSES
	draw_lock();
	wnoutrefresh(rule->window);
	draw_unlock();
#endif
}

static inline void draw_string(struct rule* rule, int y, int x, const char* str){
#ifdef USE_NOTCURSES
	ncplane_putstr_yx(rule->window, y, x, str);
#else
	draw_lock();
	mvwaddstr(rule->window, y, x, str);
	draw_unlock();
#endif
}

#include "external_command.h"
#ifdef USE_NOTCURSES
#include "plot.h"
#include "image.h"
#endif

//print a string
void* print_string(void* input){
	struct rule* rule = input;
	draw_string(rule, 0, 0, rule->data);
	stage_refresh(rule);
	return NULL;
}

//shows the date and time in a user-configured string
void* timedate(void* input){
	struct rule* rule = input;
	int h, w;
	get_size(rule, &h, &w);
	size_t size = w*MB_CUR_MAX + 1; //w columns, up to MB_CUR_MAX bytes each for multi-byte output, +1 for the NULL terminator

	char* str = malloc(size);
	if(!str) return NULL; //check for failed malloc
	pthread_cleanup_push(free, str); //free str on thread cancel

	time_t t;
	struct tm* tm;
	while(1){
		t = time(NULL);
		tm = localtime(&t);
		strftime(str, size, rule->data, tm);
		str[size-1] = '\0'; //on overflow, strftime returns 0 and buffer is undefined, so add NULL terminator just in case
		draw_string(rule, 0, 0, str);
		stage_refresh(rule);
		sleep(rule->time);
	}
	pthread_cleanup_pop(1);	//unreachable, balances pthread_cleanup_push macro
	return NULL;
}
